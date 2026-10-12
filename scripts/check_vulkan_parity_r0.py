"""Accept R0 evidence only; never promote rendering-feature or R10 acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import math
import subprocess
from run_vulkan_parity_baseline import fixture_pixels
from vulkan_parity_inventory import build, ROOT, OUT


def validate(ledger, startup, world):
    errors=[]
    ids=[entry['id'] for entry in ledger['entries']]
    if len(ids)!=len(set(ids)): errors.append('Duplicate acceptance IDs')
    for entry in ledger['entries']:
        for key in ('reference_sources','native_owner','phase','dependencies','implementation','remaining_gap'):
            if not entry.get(key): errors.append(f"Unassigned {entry['id']}: {key}")
        if entry.get('category')=='SURVEY' and not entry.get('reference_seams'):
            errors.append('Missing exact survey reference seams: '+entry['id'])
        for key in ('source_qualified','software_runtime_qualified','authenticated_runtime_qualified'):
            if type(entry.get(key)) is not bool: errors.append(f"Missing scoped qualification flag: {entry['id']}.{key}")
        if any(entry.get(k) for k in ('source_qualified','software_runtime_qualified','authenticated_runtime_qualified')) and not entry.get('evidence'):
            errors.append('Feature accepted without evidence: '+entry['id'])
    for label, report in (('startup',startup),('world',world)):
        if not report.get('startup_baseline_measured'): errors.append('Missing reproducible production captures: '+label)
        cases=report.get('cases',[])
        if len(cases)!=2 or {c.get('backend') for c in cases}!={'OpenGL','Vulkan'}:
            errors.append('Missing independent peer backend results: '+label)
        for case in cases:
            if not case.get('passed'): errors.append('Failed baseline case: '+label+'.'+case.get('backend','unknown'))
            if not case.get('viewer',{}).get('device'): errors.append('Missing device/capability facts: '+label)
            if not case.get('image_sha256'): errors.append('Missing actual readback identity: '+label)
            if not case.get('repeatability') or not all(r.get('matched') for r in case['repeatability']):
                errors.append('Unstable/unverified replay output: '+label)
            for key in ('pre_render_cpu_ms','render_submit_cpu_ms','capture_wait_and_io_ms','presentation_cpu_ms','observed_total_frame_cpu_ms'):
                value=case.get('metrics',{}).get(key,{})
                if value.get('count')!=5 or not math.isfinite(value.get('median',float('nan'))):
                    errors.append('Missing measured baseline: '+label+'.'+key)
        if not report.get('reference_comparison'): errors.append('Missing retained cross-backend comparison: '+label)
        if not report.get('asset_sha256') or not report.get('executable_sha256') or not report.get('contract_sha256'):
            errors.append('Missing revision/assets/contract identity: '+label)
    if startup.get('source_commit')!=world.get('source_commit') or not startup.get('source_commit'):
        errors.append('Baseline source revisions disagree or are uncommitted')
    if startup.get('executable_sha256')!=world.get('executable_sha256'):
        errors.append('Baseline viewer executables disagree')
    if not world.get('world_baseline_measured'): errors.append('No production world replay witness')
    for case in world.get('cases',[]):
        if not case.get('viewer',{}).get('fixed_world_clock_applied'): errors.append('Fixed world clock missing')
        if 'initial_scene_decode_cpu_ms' not in case.get('viewer',{}).get('initialization_metrics',{}):
            errors.append('Production scene decode not measured')
        for frame in case.get('viewer',{}).get('frames',[]):
            stages=frame.get('stages',{})
            if not math.isfinite(stages.get('packet_build_cpu_ms',float('nan'))): errors.append('Packet construction not measured')
            if not math.isfinite(stages.get('fixture_service_poll_cpu_ms',float('nan'))): errors.append('Fixture service polling not measured')
            # This first corpus has no avatars. Preserve that limitation rather
            # than disguising missing skinning measurements as performance data.
            if stages.get('skinning_workload_avatars')!=0: errors.append('Unexpected skinning workload')
            if case.get('backend')=='OpenGL':
                counters=frame.get('counters',{})
                if not {'draw_calls','vertex_index_upload_bytes','vertex_buffer_bind_calls'}<=set(counters):
                    errors.append('Missing GL upload/draw/bind counters')
                if 'scene_update_cpu_ms' not in stages: errors.append('GL scene update not measured')
            else:
                if not {'draw_calls','mesh_upload_bytes','binding_allocations','resource_commits','constant_upload_bytes'}<=set(stages):
                    errors.append('Missing native upload/draw/bind counters')
    return errors


def verify_files(report, report_path, source_commit):
    errors=[]
    provenance=report.get('build_provenance',{})
    if not provenance.get('complete') or provenance.get('source_commit')!=source_commit or report.get('source_commit')!=source_commit:
        errors.append('Build/inventory/source identity mismatch: '+str(report_path))
    executable=Path(provenance.get('executable',''))
    if not executable.is_file() or hashlib.sha256(executable.read_bytes()).hexdigest()!=report.get('executable_sha256') or provenance.get('executable_sha256')!=report.get('executable_sha256'):
        errors.append('Staged executable differs from qualified build: '+str(report_path))
    stage=Path(report.get('stage','')).resolve()
    for relative,expected in report.get('asset_sha256',{}).items():
        asset=(stage/relative).resolve()
        if not asset.is_relative_to(stage) or not asset.is_file() or hashlib.sha256(asset.read_bytes()).hexdigest()!=expected:
            errors.append('Changed or missing staged asset: '+relative)
    artifacts=report.get('artifact_sha256',{})
    if not artifacts: errors.append('No verifiable artifact manifest: '+str(report_path))
    for relative,expected in artifacts.items():
        path=(report_path.parent/relative).resolve()
        if not path.is_relative_to(report_path.parent.resolve()) or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
            errors.append('Changed or missing baseline artifact: '+relative)
    for case in report.get('cases',[]):
        prefix=case.get('backend','').lower()
        for image,expected in case.get('image_sha256',{}).items():
            relative=prefix+'/'+image
            if artifacts.get(relative)!=expected: errors.append('Readback not bound to artifact manifest: '+relative)
            if report.get('world_baseline_measured'):
                try: fixture_pixels(report_path.parent/relative)
                except (OSError,ValueError) as exc: errors.append(str(exc))
    if report.get('contract_sha256')!=hashlib.sha256((OUT/'baseline-contract.json').read_bytes()).hexdigest():
        errors.append('Capture contract differs from current inventory')
    return errors


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--startup',required=True,type=Path)
    p.add_argument('--world',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    args=p.parse_args()
    ledger_path=OUT/'acceptance-ledger.json'
    ledger=json.loads(ledger_path.read_text())
    if ledger!=build(): raise SystemExit('R0 inventory differs from current source')
    startup=json.loads(args.startup.read_text());world=json.loads(args.world.read_text())
    errors=validate(ledger,startup,world)
    source_commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    errors+=verify_files(startup,args.startup,source_commit)
    errors+=verify_files(world,args.world,source_commit)
    result=dict(schema=1,r0_complete=not errors,errors=errors,
        inventory_sha256=hashlib.sha256(ledger_path.read_bytes()).hexdigest(),
        source_commit=startup.get('source_commit'),full_feature_parity=False,
        acceptance_scope='Source requirement accounting and initial startup/static-primitive production baseline.',
        qualification_limits=['Static scene has no avatars; skinning performance is not exercised.',
            'GPU timing unavailable; captured frames include separately measured readback overhead.',
            'Cross-backend image differences remain rendering acceptance gaps for later phases.',
            'No real-account or physical-vendor-driver qualification.'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    if errors: raise SystemExit('\n'.join(errors))
    print('PASS: R0 initial controlled baseline; full rendering/features remain unqualified')


if __name__=='__main__': main()
