"""Test production transport options with bundled curl and a local HTTP server.

Windows uses build-vc170-64/packages; other platforms use pkg-config libcurl.
No external service or capability URL is contacted.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
request = (ROOT / 'indra/llcorehttp/_httpoprequest.cpp').read_text()
start = request.index('    // A near-zero speed threshold')
options = request[start:request.index('    // Request headers', start)]
policy = (ROOT / 'indra/llcorehttp/_httppolicy.cpp').read_text()
start = policy.index('                const bool retry_ready =')
selection = policy[start:policy.index('                op->stageFromReady', start)]
fixture = r"""
#include <curl/curl.h>
#include <cassert>
#include <memory>
#include <queue>
#include <chrono>
#include <string>
#include <iostream>
struct Options {
 bool enabled=true;
 unsigned getLowSpeedTime() const {return enabled?20:0;}
 bool getHttp11Only() const {return enabled;}
 bool getFairRetries() const {return enabled;}
};
template<class T> void check_curl_easy_setopt(CURL* c, CURLoption key,T value) {
 assert(curl_easy_setopt(c,key,value)==CURLE_OK);
}
struct HttpOpRequest {
 using ptr_t=std::shared_ptr<HttpOpRequest>;
 long mPolicyRetryAt=0;
 std::shared_ptr<Options> mReqOptions=std::make_shared<Options>();
};
struct Queue:std::queue<HttpOpRequest::ptr_t> {auto top(){return front();}};
struct State {bool mLastDispatchWasRetry=false;};
int dispatch(Queue& retryq,Queue& readyq,State& state,long now) {
 for(;;) {
""" + selection + r"""
 return take_retry?1:0;
 }
 return -1;
}
void fairness() {
 Queue retries, fresh; State state;
 for(int i=0;i<3;++i) { retries.push(std::make_shared<HttpOpRequest>()); fresh.push(std::make_shared<HttpOpRequest>()); }
 for(int expected:{1,0,1,0,1,0}) assert(dispatch(retries,fresh,state,0)==expected);
 assert(dispatch(retries,fresh,state,0)==-1);
 auto delayed=std::make_shared<HttpOpRequest>(); delayed->mPolicyRetryAt=10;
 retries.push(delayed);fresh.push(std::make_shared<HttpOpRequest>());
 assert(dispatch(retries,fresh,state,0)==0);
 assert(dispatch(retries,fresh,state,0)==-1);
 assert(dispatch(retries,fresh,state,10)==1);
 state={};
 for(int i=0;i<2;++i) { auto op=std::make_shared<HttpOpRequest>();op->mReqOptions->enabled=false;retries.push(op);fresh.push(std::make_shared<HttpOpRequest>()); }
 for(int expected:{1,1,0,0}) assert(dispatch(retries,fresh,state,0)==expected);
}
size_t sink(char*,size_t a,size_t b,void*){return a*b;}
int main(int argc,char** argv) {
 fairness(); assert(argc==3); curl_global_init(CURL_GLOBAL_ALL);
 CURL* mCurlHandle=curl_easy_init(); assert(mCurlHandle);
 auto mReqOptions=std::make_shared<Options>();
""" + options + r"""
 curl_easy_setopt(mCurlHandle,CURLOPT_URL,argv[1]);
 curl_easy_setopt(mCurlHandle,CURLOPT_PROXY,"");
 curl_easy_setopt(mCurlHandle,CURLOPT_WRITEFUNCTION,sink);
 curl_easy_setopt(mCurlHandle,CURLOPT_TIMEOUT,60L);
 const auto start=std::chrono::steady_clock::now();
 auto result=curl_easy_perform(mCurlHandle);
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 long version=0;curl_easy_getinfo(mCurlHandle,CURLINFO_HTTP_VERSION,&version);
 if(std::string(argv[2])=="stall") {assert(result==CURLE_OPERATION_TIMEDOUT);assert(seconds>=18 && seconds<35);}
 else {assert(result==CURLE_OK);assert(version==CURL_HTTP_VERSION_1_1);}
 std::cout<<argv[2]<<" result="<<result<<" seconds="<<seconds<<std::endl;
 curl_easy_cleanup(mCurlHandle);curl_global_cleanup();
}
"""

class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass
    def do_GET(self):
        self.close_connection = True
        if self.path == '/no-response':
            time.sleep(40)
            return
        self.send_response(200)
        self.send_header('Content-Length', '80')
        self.end_headers()
        try:
            for _ in range(8):
                self.wfile.write(b'x' * 10)
                self.wfile.flush()
                time.sleep(40 if self.path == '/partial-stall' else 3)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

with tempfile.TemporaryDirectory(prefix='mesh-http-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    cmake = ('cmake_minimum_required(VERSION 3.20)\nproject(mesh_http LANGUAGES CXX)\n'
             'add_executable(mesh_http test.cpp)\ntarget_compile_features(mesh_http PRIVATE cxx_std_17)\n')
    if sys.platform == 'win32':
        packages = (ROOT / 'build-vc170-64/packages').as_posix()
        cmake += f'target_include_directories(mesh_http PRIVATE "{packages}/include")\n'
        cmake += 'target_compile_definitions(mesh_http PRIVATE CURL_STATICLIB)\n'
        libs = ' '.join(f'"{packages}/lib/release/{lib}.lib"' for lib in ['libcurl','libssl','libcrypto','nghttp2','zlib'])
        cmake += f'target_link_libraries(mesh_http PRIVATE {libs} ws2_32 crypt32 wldap32 normaliz)\n'
    else:
        cmake += 'find_package(PkgConfig REQUIRED)\npkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)\ntarget_link_libraries(mesh_http PRIVATE PkgConfig::CURL)\n'
    # Keep assertions enabled while linking the viewer's release dependencies.
    cmake += 'target_compile_options(mesh_http PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)\n'
    (path / 'CMakeLists.txt').write_text(cmake)
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(path / 'build'), '--config', 'RelWithDebInfo'], check=True)
    exe = path / 'build' / ('RelWithDebInfo/mesh_http.exe' if sys.platform == 'win32' else 'mesh_http')
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for route, expected in [('progress','ok'),('no-response','stall'),('partial-stall','stall')]:
            subprocess.run([str(exe), f'http://127.0.0.1:{server.server_port}/{route}', expected], check=True, timeout=45)
    finally:
        server.shutdown()
        server.server_close()
