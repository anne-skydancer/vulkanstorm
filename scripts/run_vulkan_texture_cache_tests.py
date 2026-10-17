"""Qualify real native texture streaming and persistent-cache restart behavior."""
import argparse
import base64
import hashlib
from functools import lru_cache
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import threading
from uuid import UUID, uuid4

ROOT=Path(__file__).resolve().parents[1]
STREAM='a3000000-0000-0000-0000-000000000001'
PROFILE='60000000-0000-0000-0000-000000000006'
CASES=('cold','warm','read-only','truncated','corrupt-header','clear','resize','cache-version','codec-version')

def u64(value):
    if type(value) is int and value>=0:return value
    data=base64.b64decode(value,validate=True)
    if len(data)!=8:raise ValueError('Invalid cache counter')
    return int.from_bytes(data,'big')

@lru_cache(maxsize=1)
def full_pattern_sha256():
    digest=hashlib.sha256()
    for y in range(1023,-1,-1):
        row=bytearray()
        for x in range(1024):row.extend((255*((x//32)&1),255*((y//32)&1),255*(((x//32)+(y//32))&1),255))
        digest.update(row)
    return digest.hexdigest()

def image_rows(record):
    rows=record['texture_cache']['images']
    if not isinstance(rows,list) or len({row['id'] for row in rows})!=len(rows):raise ValueError('Invalid cached-image identities')
    return {row['id']:row for row in rows}

def validate_image(row,folder):
    if not isinstance(row,dict):raise ValueError('Invalid GPU image record')
    if row.get('gpu_matches_cpu') is not True:raise ValueError('Cached GPU pixels do not match publication')
    if not (type(row['width']) is int and type(row['height']) is int and 0<row['width']<=4096 and 0<row['height']<=4096):
        raise ValueError('Invalid GPU readback dimensions')
    if not re.fullmatch('[0-9a-f]{64}',row['gpu_rgba_sha256']):raise ValueError('Missing GPU readback digest')
    original=folder/row['gpu_rgba_file']
    path=original.resolve()
    if original.is_symlink() or not path.is_relative_to(folder.resolve()) or not path.is_file():raise ValueError('Missing or escaped GPU readback artifact')
    if path.stat().st_size!=row['width']*row['height']*4:raise ValueError('Truncated GPU image artifact')
    with path.open('rb') as source:digest=hashlib.file_digest(source,'sha256').hexdigest()
    if digest!=row['gpu_rgba_sha256']:raise ValueError('GPU artifact checksum mismatch')

def texture_requests(requests):
    if not isinstance(requests,list) or any(not isinstance(row,dict) or not isinstance(row.get('query',{}),dict) for row in requests):
        raise ValueError('Invalid texture HTTP request records')
    return [row for row in requests if row.get('query',{}).get('texture_id')]

def validate_case(record,requests,folder,cache_directory,readonly=False):
    if not isinstance(record,dict):raise ValueError('Invalid cache case record')
    if record.get('passed') is not True or record.get('source_qualified') is not True:raise ValueError('Missing source-qualified viewer acceptance')
    cache=record['texture_cache']
    if cache.get('cache_policy')!='shared_LLTextureFetch' or Path(cache['cache_path']).resolve()!=cache_directory.resolve():
        raise ValueError('Viewer did not use the intended shared cache')
    if cache.get('read_only') is not readonly:raise ValueError('Unexpected texture cache read-only policy')
    if cache.get('pending_fetches')!=0 or cache.get('pending_uploads')!=0:raise ValueError('Unfinished texture fetches at capture')
    if u64(cache['maximum_body_bytes'])<=0 or cache['maximum_entries']<=0:raise ValueError('Missing effective cache capacity')
    if not isinstance(cache['process_instance'],str):raise ValueError('Invalid cache process identity')
    UUID(cache['process_instance'])
    rows=image_rows(record)
    if not {PROFILE,STREAM}.issubset(rows):raise ValueError('Required cache images were not published')
    for row in rows.values():validate_image(row,folder)
    stages=record['r3_texture_streaming']
    for name in ('coarse','full_resolution_preference','coarse_after_preference','resolution_limit','restored'):
        validate_image(stages[name],folder)
    coarse=stages['coarse'];again=stages['coarse_after_preference']
    if not (0<coarse['discard']<=5 and coarse['width']==1024>>coarse['discard'] and coarse['height']==coarse['width']):
        raise ValueError('Projected demand did not produce the expected coarse resource')
    if any(again[key]!=coarse[key] for key in ('width','height','discard')):
        raise ValueError('Full-resolution preference did not restore normal projected demand')
    # A partial J2C stream can legitimately refine quality at the same discard.
    # Compare each stage to the shared reference decoder's exact input window,
    # rather than assuming that 600 bytes and a complete codestream are equal.
    for stage,reference in (('coarse','coarse-partial'),('coarse_after_preference','coarse-complete'),
                            ('resolution_limit','resolution-limit')):
        row=stages[stage]
        expected=dict(row,gpu_rgba_file=stages['references'][reference])
        validate_image(expected,folder)
    for name in ('full_resolution_preference','restored'):
        row=stages[name]
        if (row['width'],row['height'],row['discard'])!=(1024,1024,0) or row['gpu_rgba_sha256']!=full_pattern_sha256():
            raise ValueError('Full-resolution cache pixels differ from the independent lossless pattern')
    limited=stages['resolution_limit']
    if (limited['width'],limited['height'],limited['discard'])!=(512,512,1):raise ValueError('Native maximum-resolution preference was inert')
    if rows[STREAM]['gpu_rgba_sha256']!=full_pattern_sha256():raise ValueError('Final resident texture differs from the expected full-resolution pixels')
    retired=record['retired_texture_cache']
    if retired['images'] or retired['pending_fetches']!=0 or retired.get('pending_uploads')!=0:raise ValueError('Retired account retained cached GPU images or fetches')
    if retired['process_instance']!=cache['process_instance']:raise ValueError('Cache lifecycle evidence changed process identity')
    if u64(retired['network_requests'])!=len(texture_requests(requests)):raise ValueError('Texture HTTP evidence disagrees with the worker counter')
    return rows

def snapshot(cache_directory):
    root=cache_directory/'texturecache'
    files={}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():raise RuntimeError('Unexpected symlink in isolated texture cache')
        if path.is_file():
            with path.open('rb') as source:digest=hashlib.file_digest(source,'sha256').hexdigest()
            files[str(path.relative_to(cache_directory))]=dict(bytes=path.stat().st_size,sha256=digest)
    if not files:raise RuntimeError('No persistent texture cache files were written')
    return files

def mutate_entry(cache_directory,kind):
    root=cache_directory.resolve();directory=root/'texturecache'
    if kind=='truncated':
        targets=list(directory.rglob(STREAM+'.texture'))
        if len(targets)!=1:raise RuntimeError('Missing unique streaming texture body')
        path=targets[0];before=path.read_bytes()
        if not before:raise RuntimeError('Streaming body was already empty')
        after=b'';offset=0
    else:
        entries=(directory/'texture.entries').read_bytes();positions=[];start=44
        while start+28<=len(entries):
            if entries[start:start+16]==UUID(STREAM).bytes:positions.append((start-44)//28)
            start+=28
        if len(positions)!=1:raise RuntimeError('Missing unique streaming texture header entry')
        path=directory/'texture.cache';before=path.read_bytes();offset=positions[0]*600
        if before[offset:offset+2]!=b'\xff\x4f':raise RuntimeError('Unexpected J2C cache header prefix')
        after=before[:offset]+b'\0\0'+before[offset+2:]
    if not path.resolve().is_relative_to(root):raise RuntimeError('Refusing mutation outside the isolated cache')
    path.write_bytes(after)
    return dict(file=str(path.relative_to(root)),offset=offset,before_bytes=len(before),after_bytes=len(after),
                before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=hashlib.sha256(after).hexdigest())

def run_driver(command,folder,timeout=540):
    """Preserve driver failures even if Python exits before writing viewer results."""
    folder=Path(folder);errors=[];timed_out=False
    with (folder/'driver-output.log').open('w',encoding='utf-8') as output:
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                 text=True,encoding='utf-8',errors='replace')
        def drain():
            try:
                for line in process.stdout:
                    output.write(line);output.flush();print(line,end='',flush=True)
            except (OSError,ValueError) as error:errors.append(str(error))
            finally:process.stdout.close()
        reader=threading.Thread(target=drain,daemon=True);reader.start()
        try:code=process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out=True;process.kill();code=process.wait(timeout=10)
        finally:
            reader.join(timeout=10)
        if reader.is_alive():errors.append('Replay driver output did not close')
    (folder/'driver.json').write_text(json.dumps(dict(return_code=code,timed_out=timed_out,
        output_file='driver-output.log',output_errors=errors),indent=2)+'\n')
    if timed_out:raise subprocess.TimeoutExpired(command,timeout)
    if errors:raise RuntimeError('Cannot preserve replay driver output: '+'; '.join(errors))
    return code


def validate_driver(folder):
    folder=Path(folder);record=json.loads((folder/'driver.json').read_text())
    if (record.get('return_code')!=0 or record.get('timed_out') is not False or
        record.get('output_errors')!=[] or record.get('output_file')!='driver-output.log'):
        raise ValueError('Unsuccessful or incomplete cache replay driver')
    output=folder/'driver-output.log'
    if output.is_symlink() or not output.is_file() or output.stat().st_size==0:
        raise ValueError('Missing cache replay driver output')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--harness-evidence',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--register-windows-manifests',action='store_true')
    args=parser.parse_args()
    directory=args.evidence.resolve();directory.mkdir(parents=True,exist_ok=True)
    run=directory/('run-'+uuid4().hex);run.mkdir()
    base_cache=run/'shared-cache';results={};reports={};reference=None;processes=set();started=time.monotonic()
    for case in CASES:
        folder=run/case;folder.mkdir()
        cache=base_cache if case in ('cold','warm') else run/('cache-'+case)
        record=dict(passed=False,evidence=folder.relative_to(directory).as_posix())
        try:
            if case not in ('cold','warm'):
                cache.mkdir();shutil.copytree(base_cache/'texturecache',cache/'texturecache')
            policy=None
            if case!='cold':
                values=dict(reports['cold']['texture_cache']['startup_policy'])
                if case=='cache-version':values['LocalCacheVersion']-=1
                if case=='codec-version':values['LastJ2CVersion']='R3 intentional previous codec revision'
                policy=folder/'cache-policy.json';policy.write_text(json.dumps(values,indent=2)+'\n')
            if case in ('truncated','corrupt-header'):record['mutation']=mutate_entry(cache,case)
            before=snapshot(cache) if case=='read-only' else None
            if before is not None:record['cache_before']=before
            command=[sys.executable,str(ROOT/'scripts/run_vulkan_session_replay.py'),
                     '--build-directory',str(args.build_directory.resolve()),'--runtime',str(args.runtime.resolve()),
                     '--harness-evidence',str(args.harness_evidence.resolve()),'--evidence',str(folder),
                     '--headless','--texture-cache-contracts','--cache-directory',str(cache),
                     '--texture-fixture',str(ROOT/'indra/newview/skins/default/textures/uv_test1.j2c')]
            if policy:command.extend(['--cache-policy',str(policy)])
            if case=='cold':command.append('--local-image-contracts')
            if case=='read-only':command.append('--texture-cache-read-only')
            if case=='clear':command.append('--cache-clear')
            if case=='resize':
                original=reports['cold']['texture_cache']['configured_cache_size_mb']
                command.extend(['--cache-size',str(512 if original==256 else 256)])
            if args.register_windows_manifests:command.append('--register-windows-manifests')
            print(f'RUN: R3 texture cache {case}',flush=True)
            code=run_driver(command,folder)
            if code:raise RuntimeError(f'Replay driver exited with code {code}; see driver-output.log')
            validate_driver(folder)
            if not (folder/'results.json').is_file():
                raise RuntimeError(f'Replay driver exited with code {code} without results.json; see driver-output.log')
            child=json.loads((folder/'results.json').read_text());requests=json.loads((folder/'http-replay.json').read_text())
            if code:raise RuntimeError(f'Viewer cache case returned {code}')
            rows=validate_case(child,requests,folder,cache,case=='read-only')
            instance=child['texture_cache']['process_instance']
            if instance in processes:raise RuntimeError('Cache cases reused a viewer process instance')
            processes.add(instance)
            if reference is None:reference=rows
            else:
                for identity in (PROFILE,STREAM):
                    if rows[identity]['gpu_rgba_sha256']!=reference[identity]['gpu_rgba_sha256']:
                        raise RuntimeError('Restart changed deterministic cached image pixels')
            traffic=texture_requests(requests)
            if case in ('warm','read-only','resize') and traffic:raise RuntimeError('Warm resident textures unexpectedly used the network')
            if case in ('cold','truncated','corrupt-header','clear','cache-version','codec-version') and not traffic:
                raise RuntimeError('Missing required network recovery evidence')
            if case=='cold':
                ranges=[request for request in traffic if request['query']['texture_id']==[STREAM]]
                if not any(request.get('range_start')==0 for request in ranges) or not any(request.get('range_start',0)>0 for request in ranges):
                    raise RuntimeError('Missing real progressive continuation range requests')
            if case=='read-only' and snapshot(cache)!=before:raise RuntimeError('Read-only texture cache files changed')
            if case=='resize':
                old=reports['cold']['texture_cache'];new=child['texture_cache']
                if old['maximum_body_bytes']==new['maximum_body_bytes'] or old['maximum_entries']==new['maximum_entries']:
                    raise RuntimeError('Cache size preference did not change effective capacity')
            record.update(passed=True,texture_requests=len(traffic),process_instance=instance,
                          cache_files=snapshot(cache),source_commit=child['source_commit'])
            reports[case]=child
        except (OSError,ValueError,KeyError,TypeError,RuntimeError,subprocess.TimeoutExpired) as error:
            record['failure']=str(error)
        results[case]=record
        print(('PASS' if record['passed'] else 'FAIL')+f': R3 texture cache {case}'+('' if record['passed'] else ': '+record['failure']),flush=True)
        if not record['passed']:break
    complete=set(results)==set(CASES);passed=complete and all(case['passed'] for case in results.values())
    report=dict(passed=passed,complete=complete,planned_cases=list(CASES),cases=results,
                seconds=time.monotonic()-started,live_server_qualified=False,
                qualification_limit='Read-only exercises the shared sibling cache policy; concurrent live viewer arbitration is separate.')
    (directory/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    return 0 if passed else 1

if __name__=='__main__':raise SystemExit(main())
