"""Compile the production receive callback and retry policy without a window."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class NativeHTTPBodyLimit(unittest.TestCase):
    def test_receive_bounds_and_terminal_retry_policy(self):
        callback = method((ROOT / 'indra/llcorehttp/_httpoprequest.cpp').read_text(),
                          'size_t HttpOpRequest::writeCallback(')
        common = (ROOT / 'indra/llcorehttp/httpcommon.cpp').read_text()
        retry = method(common, 'bool HttpStatus::isRetryable() const')
        errors = (ROOT / 'indra/llcorehttp/httpcommon.h').read_text()
        errors = errors[errors.index('enum HttpError'):errors.index('}; // end enum HttpError') + 2]
        options = (ROOT / 'indra/llcorehttp/httpoptions.h').read_text()
        setter = method(options, 'void setMaximumResponseBodyBytes(')
        getter = method(options, 'std::size_t getMaximumResponseBodyBytes() const')
        curl_names = sorted(set(re.findall(r'CURLE_\w+', retry)))
        fixture = r'''
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK
ERRORS
enum CurlErrors { CURL_NAMES };
struct HttpStatus {
 enum { EXT_CURL_EASY=0, LLCORE=2 };
 int type=LLCORE, code=0;
 HttpStatus()=default;
 HttpStatus(int t,int c):type(t),code(c){}
 explicit HttpStatus(int t):type(t),code(t<200||t>=300){}
 explicit operator bool()const{return code==0;}
 bool isHttpStatus()const{return type>=100;}
 int getType()const{return type;}
 bool operator==(const HttpStatus& b)const{return type==b.type&&code==b.code;}
 bool isRetryable()const;
};
RETRY
struct Options {
 std::size_t mMaximumResponseBodyBytes = 0;
 SETTER
 GETTER
};
struct BufferArray {
 std::size_t bytes=0;
 unsigned calls=0;
 std::size_t size()const{return bytes;}
 std::size_t append(char*,std::size_t n){++calls;bytes+=n;return n;}
};
struct HTTPStats {
 std::size_t received=0;
 static HTTPStats& instance(){static HTTPStats stats;return stats;}
 void recordDataDown(std::size_t n){received+=n;}
};
struct HttpOpRequest {
 using ptr_t=HttpOpRequest*;
 std::shared_ptr<Options> mReqOptions;
 BufferArray* mReplyBody=nullptr;
 HttpStatus mStatus;
 ~HttpOpRequest(){delete mReplyBody;}
 template<class T>static T* fromHandle(void* p){return static_cast<T*>(p);}
 static size_t writeCallback(void*,size_t,size_t,void*);
};
CALLBACK
int main(){
 char chunk[1]{};
 // Limits do not depend on Content-Length, HTTP status or Range compliance.
 // Chunked success and error bodies take exactly the same production path.
 for(int status : {200,206,404,500}){
  HttpOpRequest op;op.mStatus=HttpStatus(status);
  op.mReqOptions=std::make_shared<Options>();op.mReqOptions->setMaximumResponseBodyBytes(8);
  assert(HttpOpRequest::writeCallback(chunk,1,3,&op)==3);
  assert(HttpOpRequest::writeCallback(chunk,1,5,&op)==5);
  const auto accounted=HTTPStats::instance().received;
  assert(HttpOpRequest::writeCallback(chunk,1,1,&op)==0);
  assert(op.mReplyBody->size()==8&&op.mReplyBody->calls==2);
  assert(HTTPStats::instance().received==accounted);
  assert(op.mStatus==HttpStatus(HttpStatus::LLCORE,HE_RESPONSE_TOO_LARGE));
  assert(!op.mStatus.isRetryable());
 }
 // Reject an oversized first chunk before even creating the reply buffer.
 HttpOpRequest first;first.mReqOptions=std::make_shared<Options>();
 first.mReqOptions->setMaximumResponseBodyBytes(8);
 assert(HttpOpRequest::writeCallback(chunk,1,9,&first)==0&&!first.mReplyBody);
 // Existing callers stay unlimited; multiplication and addition still cannot wrap.
 for(bool configured : {false,true}){
  HttpOpRequest op;if(configured)op.mReqOptions=std::make_shared<Options>();
  assert(HttpOpRequest::writeCallback(chunk,1,100,&op)==100);
  assert(HttpOpRequest::writeCallback(chunk,2,std::numeric_limits<size_t>::max(),&op)==0);
  assert(op.mReplyBody->size()==100&&op.mReplyBody->calls==1);
  op.mReplyBody->bytes=std::numeric_limits<size_t>::max()-1;
  assert(HttpOpRequest::writeCallback(chunk,1,2,&op)==0);
  assert(op.mReplyBody->calls==1&&!op.mStatus.isRetryable());
 }
 assert(HttpStatus(500).isRetryable()); // Existing transient failure policy unchanged.
}
'''
        fixture = (fixture.replace('ERRORS', errors).replace('CURL_NAMES', ','.join(curl_names))
                   .replace('RETRY', retry).replace('SETTER', setter).replace('GETTER', getter)
                   .replace('CALLBACK', callback))
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'body_limit.cpp'
            executable = Path(directory) / 'body_limit.exe'
            source.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            self.assertEqual(subprocess.run([str(executable)]).returncode, 0)

    def test_native_limits_and_failure_delivery(self):
        mesh = (ROOT / 'indra/newview/vsworldassets.cpp').read_text()
        texture = (ROOT / 'indra/newview/vsuiimageprovider.cpp').read_text()
        asset = (ROOT / 'indra/newview/llviewerassetstorage.cpp').read_text()
        self.assertIn('setMaximumResponseBodyBytes(wireLimit)', mesh)
        fetch = (ROOT / 'indra/newview/lltexturefetch.cpp').read_text()
        self.assertIn('setMaximumResponseBodyBytes(16 * 1024 * 1024)', fetch)
        self.assertIn('setMaximumResponseBodyBytes(64 * 1024 * 1024)', asset)
        self.assertIn('LLCore::HE_RESPONSE_TOO_LARGE', texture)
        failure = texture[texture.index('LLCore::HE_RESPONSE_TOO_LARGE'):]
        self.assertLess(failure.index('failedGeneration=generation'), failure.index('entry.attempts=3'))
        terminal = fetch[fetch.index('if (mState == WAIT_HTTP_REQ)'):]
        self.assertLess(terminal.index('HE_RESPONSE_TOO_LARGE'), terminal.index('if (mRequestedSize < 0)'))
        curl = (ROOT / 'indra/llcorehttp/_httplibcurl.cpp').read_text()
        self.assertIn('if (op->mStatus)\n    {\n        op->mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, status);', curl)
        self.assertIn('op->mXLLURL.size() && op->mStatus != HttpStatus(HttpStatus::LLCORE, HE_RESPONSE_TOO_LARGE)', curl)


if __name__ == '__main__':
    unittest.main()
