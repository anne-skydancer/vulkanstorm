"""Replay the native CPU session through the fully staged viewer, without grid credentials."""
import vulkan_ui_environment_fixture as ui_environment_fixture
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import zlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import threading
import tempfile
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import xml.etree.ElementTree as ET
import xmlrpc.client
from uuid import UUID, uuid5, NAMESPACE_URL
from urllib.parse import urlsplit, parse_qs

import vulkan_r2_fixture as r2_fixture
import vulkan_r3_gltf_fixture as r3_gltf_fixture

from run_software_vulkan_tests import windows_manifest_registration, loaded_library_hashes
from run_vulkan_viewer_diagnostic import runtime_libraries, skin_cases, report_case
from vulkan_viewer_process import run_viewer_process, run_headless_viewer_process, process_isolation_evidence

# Vintage's full 396-frame Preferences traversal takes about 200 seconds on
# the pinned software device; all connected actions took over 314 seconds.
VIEWER_REPLAY_SECONDS = 480
PROCESS_REPLAY_SECONDS = 500
CASE_REPLAY_SECONDS = 530


def harness_prerequisite_passed(harness, headless=False):
    if not harness.get('passed'):
        return False
    if not headless:
        return harness.get('presentation_qualified') is True
    # A no-window contract replay has its own prerequisite; it never supplies
    # evidence for WSI or relaxes the ordinary presentation qualification.
    modes = {record.get('mode') for record in harness.get('tests', [])
             if record.get('passed') is True}
    return {'offscreen', 'world'}.issubset(modes)


def r1_contracts_passed(record, requests):
    contracts=record.get('r1_contracts', {})
    if not all(contracts.get(key) is True for key in (
            'host_ids','object_face_retirement','variable_terrain','asset_replacement',
            'crossing_continuity','circuit_retirement','inventory_scope','inventory_revision','scene_owner_kinds','self_continuity')):
        return False
    if not all(key in contracts for key in ('motion_selection','task_inventory','appearance_startup','logout_retirement')):
        return False
    try:
        def u64(value):
            data=base64.b64decode(value,validate=True)
            if len(data)!=8:raise ValueError('Invalid LLSD U64')
            return int.from_bytes(data,'big')
        motion=contracts['motion_selection'];task=contracts['task_inventory'];retired=contracts['logout_retirement']
        if motion['authoritative_position']!=[15,20,30] or motion['selected'] is not False or motion['predicted_position']==[15,20,30]:
            return False
        if not all(math.isfinite(value) for value in motion['predicted_position']+motion['predicted_rotation']):return False
        if not task['known'] or task['pending'] or task['serial']!=42 or task['filename']!='r1-task.txt':return False
        if not contracts['appearance_startup']:return False
        if retired['owners'] or retired['neighbours'] or retired['owned_object_slots'] or retired['owned_avatar_slots']:
            return False
        if retired['assets']['requests'] or u64(retired['assets']['resident_bytes']):return False
        before=contracts['mesh_before'];after=contracts['mesh_after']
        if u64(after['asset_generation'])<=u64(before['asset_generation']):return False
        if abs(before['asset_third_vertex'][0]-(32768/65535-.5))>1e-4 or abs(after['asset_third_vertex'][0]-(45000/65535-.5))>1e-4:
            return False
        if not any(region['width']==1024 and region['height']==512 and region['terrain_patches']==1
                   for region in contracts['initial_scene']['neighbours']):return False
    except (KeyError,TypeError,ValueError):
        return False
    return {1,2}.issubset({request.get('revision') for request in requests if request.get('path')=='/r1-mesh'})


def body_lods_passed(body):
    """Require every shipped body mesh/LOD and every actual viewport case."""
    try:
        if body['all_parts_and_lods'] is not True or body['unchanged_pose_reuses_geometry'] is not True:
            return False
        catalog = Path(__file__).resolve().parents[1]/'indra/newview/character/avatar_lad.xml'
        expected = {(mesh.attrib['type'], int(mesh.attrib['lod'])): float(mesh.attrib['min_pixel_width'])**2
                    for mesh in ET.parse(catalog).getroot().findall('mesh')}
        parts = {name for name, _ in expected}
        cases = body['cases']
        observed = set()
        heights = {1 << power for power in range(16)}
        visited = set()
        for row in cases:
            identity = (row['part_name'], row['lod'])
            case = (row['part_name'], row['height'])
            if (identity not in expected or row['height'] not in heights or case in visited or
                    row['reference_area'] != expected[identity] or not 0 < row['vertices'] <= 65536 or
                    not 0 < row['indices'] <= 131072*3 or row['indices'] % 3):
                return False
            observed.add(identity)
            visited.add(case)
        return observed == set(expected) and visited == {(part, height) for part in parts for height in heights}
    except (KeyError, TypeError, ValueError, OSError, ET.ParseError):
        return False


def local_image_contracts_passed(record,directory):
    """Check actual PNG file reloads against independently specified GPU bytes."""
    from run_vulkan_texture_cache_tests import u64
    try:
        data=record['r3_local_images'];folder=Path(directory).resolve()
        if any(data.get(key) is not True for key in ('stable_facade','retained_generation_unchanged','default_alias')):
            return False
        if data['updates']!=2 or data['retirements']!=2:return False
        generations=[];identity=None
        for name,width,height,phase in (('initial',32,32,0),('replaced',32,16,1),('refused',32,16,1),('recovered',16,32,2)):
            row=data[name]
            if (row['width'],row['height'],row['phase'])!=(width,height,phase):return False
            if identity is None:identity=row['id']
            if not identity or row['id']!=identity:return False
            generations.append(u64(row['generation']))
            relative=Path(row['file']);path=(folder/relative).resolve()
            if relative.is_absolute() or '..' in relative.parts or not path.is_relative_to(folder) or (folder/relative).is_symlink():return False
            expected=bytearray()
            for y in range(height-1,-1,-1):
                for x in range(width):expected.extend((255 if x<width//2 else 0,255 if y<height//2 else 0,0 if phase==0 else 255 if phase==1 else 128,255))
            if path.read_bytes()!=expected:return False
        return 0<generations[0]<generations[1]==generations[2]<generations[3]
    except (OSError,ValueError,KeyError,TypeError,AttributeError):
        return False


def local_material_contracts_passed(record,directory):
    """Verify production local PBR packing and immutable reload generations."""
    from run_vulkan_texture_cache_tests import u64
    try:
        data=record['r3_local_material'];folder=Path(directory).resolve()
        if any(data.get(key) is not True for key in
               ('stable_identities','retired','retained_generations_unchanged','native_only')):return False
        identities=None;generations=[]
        for name,phase in (('initial',0),('replaced',1),('refused',1),('recovered',1)):
            rows=data[name]
            if not isinstance(rows,list) or len(rows)!=4:return False
            ids=[row['id'] for row in rows]
            if len(set(ids))!=4 or any(str(UUID(value))=='00000000-0000-0000-0000-000000000000' for value in ids):return False
            if identities is None:identities=ids
            if identities!=ids:return False
            colors=((73,121,199,255) if not phase else (199,73,121,255),
                    (128,128,255,255),(77,64,192,255),(9,19,29,255))
            generations.append([u64(row['generation']) for row in rows])
            for row,color in zip(rows,colors):
                relative=Path(row['file']);path=(folder/relative).resolve()
                if relative.is_absolute() or '..' in relative.parts or not path.is_relative_to(folder) or (folder/relative).is_symlink():return False
                if path.read_bytes()!=bytes(color)*(16*16):return False
        return all(0<generations[0][i]<generations[1][i]==generations[2][i]<generations[3][i] for i in range(4))
    except (OSError,ValueError,KeyError,TypeError,AttributeError):return False


MEDIA_PAGE = b"""<!doctype html><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden}
body{display:grid;grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr}
div:nth-child(1){background:#112233}div:nth-child(2){background:#445566}
div:nth-child(3){background:#778899}div:nth-child(4){background:#aabbcc}
body[data-phase="2"] div:nth-child(1){background:#dd2211}body[data-phase="2"] div:nth-child(2){background:#33cc55}
body[data-phase="2"] div:nth-child(3){background:#8811bb}body[data-phase="2"] div:nth-child(4){background:#4499ee}
</style><body><div></div><div></div><div></div><div></div><script>
setInterval(async()=>{try{const value=Number(await(await fetch('/r3-media-phase',{cache:'no-store'})).text());
if(value===1||value===2)document.body.dataset.phase=String(value);}catch(e){}},100);
</script>"""


def shared_settings_contracts_passed(record,directory):
    try:
        row=record['r3_shared_settings'];root=Path(directory).resolve()
        if row.get('navbar_callback') is not True or row.get('debug_callback') is not True or row.get('persisted') is not True or row.get('font_callbacks')!=2:return False
        if row['file']!='r3/settings-roundtrip.xml':return False
        path=root/row['file']
        if path.is_symlink():return False
        saved=read_llsd(ET.fromstring(path.read_bytes()))
        for setting,key in (('ShowNavbarNavigationPanel','navigation'),('ShowNavbarFavoritesPanel','favorites'),('DebugViews','debug')):
            if type(row[key]) is not bool or saved[setting]['Type']!='Boolean' or bool(saved[setting]['Value'])!=row[key]:return False
        return saved['ChatFontSize']['Type']=='S32' and type(row['font']) is int and 0<=row['font']<4 and saved['ChatFontSize']['Value']==row['font']
    except (OSError,KeyError,ValueError,TypeError,ET.ParseError):return False


def media_contracts_passed(record,requests,directory):
    """Demand actual plugin publication, shared ownership and independent pixels."""
    from run_vulkan_texture_cache_tests import u64
    try:
        data=record['r3_media'];root=Path(directory).resolve()
        if any(data.get(key) is not True for key in ('actual_plugin','shared_manager','shared_consumer_retained','immutable_generations','retired','account_retired')):return False
        pages=[row for row in requests if row.get('path')=='/r3-media']
        if not pages or any(row.get('bytes')!=len(MEDIA_PAGE) or row.get('sha256')!=hashlib.sha256(MEDIA_PAGE).hexdigest() for row in pages):return False
        phases={row.get('phase') for row in requests if row.get('path')=='/r3-media-phase'}
        if not {1,2}<=phases:return False
        generation=0
        for name,width,height,palette in (('initial',64,64,((17,34,51),(68,85,102),(119,136,153),(170,187,204))),('updated',64,64,((221,34,17),(51,204,85),(136,17,187),(68,153,238))),('resized',78,46,((221,34,17),(51,204,85),(136,17,187),(68,153,238)))):
            row=data[name]
            if row['width']!=width or row['height']!=height or row['bits_width']<width or row['bits_height']<height or not isinstance(row['origin_opengl'],bool):return False
            current=u64(row['generation'])
            if current<=generation:return False
            generation=current
            if row['file']!=f'r3/media-{name}.rgba':return False
            path=root/row['file']
            if path.is_symlink():return False
            expected=b''.join(bytes((*palette[(y>=height//2)*2+(x>=width//2)],255)) for y in range(height) for x in range(width))
            if path.read_bytes()!=expected:return False
        return True
    except (OSError,ValueError,KeyError,TypeError,AttributeError):return False


def legacy_material_contracts_passed(record,requests,directory):
    try:
        data=record['r3_legacy_material'];root=Path(directory).resolve()
        if data.get('faces')!=6 or any(data.get(key) is not True for key in ('object_retired','account_retired')):return False
        variants=data['variants']
        if not isinstance(variants,list) or len(variants)!=6:return False
        for index,row in enumerate(variants):
            if row['fullbright'] is not (index in (2,5)) or row['emissive_alpha'] is not (index==3):return False
            if row['shiny']!=[1,2,0,3,0,0][index] or row['alpha']!=['opaque','opaque','mask','opaque','blend','opaque'][index]:return False
            if abs(row['base_alpha']-(128/255 if index==4 else 1))>1.e-5:return False
        def close(a,b):return len(a)==len(b) and all(math.isfinite(x) and abs(x-y)<1.e-5 for x,y in zip(a,b))
        def uv(s,t,x,y,angle):
            c,r=math.cos(angle),math.sin(angle)
            return [s*c,-t*r,s*r,t*c,x+.5-.5*(s*c+s*r),y+.5-.5*(-t*r+t*c)]
        if not close(data['normal_uv'],uv(2,3,.125,-.25,.25)) or not close(data['specular_uv'],uv(.75,1.25,-.125,.375,-.5)):return False
        if not close(data['specular'],[64/255,128/255,192/255,32/255]):return False
        if abs(data['roughness']-(1-128/255))>1.e-5 or abs(data['cutoff']-64/255)>1.e-5:return False
        source=root/'r3/legacy-materials.xml';binary=root/'r3/legacy-materials.bin'
        if source.is_symlink() or binary.is_symlink():return False
        payload=source.read_bytes();raw=binary.read_bytes()
        if len(payload)>2*1024*1024 or len(raw)>4*1024*1024:return False
        wrapped=read_llsd(ET.fromstring(payload))
        if zlib.decompress(base64.b64decode(wrapped['Zipped'],validate=True))!=raw:return False
        if UUID('b3400000-0000-0000-0000-000000000001').bytes not in raw:return False
        received=[row for row in requests if row.get('path')=='/r3-legacy-materials']
        if len(received)!=1 or received[0].get('bytes')!=len(payload) or received[0].get('sha256')!=hashlib.sha256(payload).hexdigest():return False
        if data['readbacks']!=['r3/legacy-0.rgba','r3/legacy-1.rgba']:return False
        for index,(identity,color) in enumerate(zip((data['normal_source'],data['specular_source']),((128,128,255,255),(11,22,33,255))),1):
            if identity!=f'b3600000-0000-0000-0000-{index:012d}' or UUID(identity).bytes not in raw:return False
            path=root/data['readbacks'][index-1]
            if path.is_symlink() or path.read_bytes()!=bytes(color)*256:return False
        return True
    except (OSError,ValueError,KeyError,TypeError,ET.ParseError,zlib.error):return False


def remote_material_contracts_passed(record,requests,directory):
    """Require actual asset HTTP bytes and successful/failed native publication."""
    try:
        data=record['r3_remote_materials'];root=Path(directory).resolve()
        if data.get('actual_object_faces')!=6 or data.get('replacement_faces')!=6 or any(data.get(key) is not True for key in
                ('object_retired','native_only','shared_parser_factors','account_retired','asset_recovered')):return False
        assets=data['assets']
        if len(assets)!=4 or [row['loaded'] for row in assets]!=[True,False,False,True]:return False
        for index,row in enumerate(assets,1):
            identity=f'b3100000-0000-0000-0000-{index:012d}'
            if row['id']!=identity or row['file']!=f'r3/remote-materials/{identity}.asset':return False
            original=root/row['file'];path=original.resolve()
            if original.is_symlink() or not path.is_relative_to(root) or not path.is_file():return False
            payload=path.read_bytes()
            received=[item for item in requests if item.get('query',{}).get('material_id')==[identity]]
            if len(received)!=(2 if index==4 else 1):return False
            if received[-1].get('bytes')!=len(payload) or received[-1].get('sha256')!=hashlib.sha256(payload).hexdigest():return False
            if index==4 and (received[0].get('bytes')!=len(bad_payload) or received[0].get('sha256')!=hashlib.sha256(bad_payload).hexdigest()):return False
            if index in (1,4):
                asset=read_llsd(ET.fromstring(payload))
                material=json.loads(asset['data'])['materials'][0]
                factors=material['pbrMetallicRoughness']
                if factors['baseColorFactor']!=[.5 if index==4 else .25,.5,.75,.625] or not material['doubleSided'] or material['alphaMode']!='MASK':return False
                if abs(factors['metallicFactor']-.3)>1.e-6 or abs(factors['roughnessFactor']-.6)>1.e-6:return False
            elif index==2:
                if read_llsd(ET.fromstring(payload))['data']!='invalid native material fixture':return False
                bad_payload=payload
            elif payload!=b' '*(1024*1024+1):return False
        return True
    except (OSError,ValueError,KeyError,TypeError,ET.ParseError):return False


def world_bump_contracts_passed(record,directory):
    """Actual ObjectUpdate faces must retain scoped, generation-cached normals."""
    from run_vulkan_texture_cache_tests import u64
    try:
        data=record['r3_world_bumps'];root=Path(directory).resolve();stages=[]
        standard_source=(Path(__file__).resolve().parents[1]/'indra/newview/app_settings/std_bump.ini').read_text().split()[4]
        if any(data.get(key) is not True for key in ('udp_object_update','stable_cached_generations','object_retired','account_retired','inactive_faces_release_images')):return False
        for name,scale in (('initial',128),('source_replaced',128),('scale_changed',64)):
            rows=data[name]
            if not isinstance(rows,list) or [row['code'] for row in rows]!=[1,2,3]:return False
            for row in rows:
                width,height=row['width'],row['height']
                if not 0<width<=4096 or not 0<height<=4096 or row['scale']!=scale:return False
                if not u64(row['generation']) or not u64(row['source_generation']) or row['components'] not in range(1,6):return False
                contents={}
                for key in ('gpu_file','expected_file','source_file'):
                    relative=Path(row[key]);path=(root/relative).resolve()
                    if relative.is_absolute() or '..' in relative.parts or not path.is_relative_to(root) or (root/relative).is_symlink():return False
                    contents[key]=path.read_bytes()
                    if len(contents[key])!=width*height*4:return False
                if max(abs(a-b) for a,b in zip(contents['gpu_file'],contents['expected_file']))>1:return False
                if row['code']==3 and row['source_id']!=standard_source:return False
                if row['code']<=2:
                    if width!=16 or height!=16 or row['source_id']!='b3000000-0000-0000-0000-000000000002':return False
                    phase=name!='initial'
                    expected=bytearray()
                    for y in range(15,-1,-1):
                        for x in range(16):expected.extend((255 if (x<8)!=phase else 0,255 if (y<8)!=phase else 0,0,255))
                    if contents['source_file']!=expected:return False
            stages.append(rows)
        for channel in range(3):
            initial,replaced,scaled=(stage[channel] for stage in stages)
            if u64(scaled['generation'])<=u64(replaced['generation']):return False
            if u64(scaled['source_generation'])!=u64(replaced['source_generation']):return False
            if channel<2:
                if u64(replaced['generation'])<=u64(initial['generation']) or u64(replaced['source_generation'])<=u64(initial['source_generation']):return False
            elif replaced['generation']!=initial['generation'] or replaced['source_generation']!=initial['source_generation']:return False
        return True
    except (OSError,ValueError,KeyError,TypeError,AttributeError):return False


def r2_contracts_passed(record, requests, directory=None, *, write_manifest=True):
    """Never promote a passing R1/UI replay into R2 qualification."""
    if not isinstance(requests, list) or any(not isinstance(request, dict) for request in requests):
        return False
    contracts = record.get('r2_contracts', {})
    required = ('ordinary_inventory_actions', 'layer_order_changed',
                'avatar_reset', 'animated_geometry', 'animated_selection',
                'hud_attachments', 'rigged_attachments', 'animesh',
                'gltf_topologies', 'gltf_animation', 'geometry_replacement',
                'bake_pixels', 'bake_capability_upload', 'bake_legacy_upload',
                'motion_gesture_ao', 'logout_retirement', 'bake_gpu_readbacks',
                'gltf_empty_replacement', 'gltf_skin_animation', 'gltf_trs_animation',
                'fitted_detach_restoration', 'script_control_transport',
                'gesture_animation_transport', 'ao_inventory_override',
                'remote_animation_selection', 'remote_visibility_restoration', 'remote_avatar_retirement',
                'ordinary_attachment_rez', 'ordinary_attachment_detach',
                'specialized_motions', 'physics_morphs', 'sitting_ground_hover')
    if not all(contracts.get(key) is True for key in required):
        return False
    observed = {request.get('path') for request in requests}
    if not {'r2-asset', 'r2-mesh', 'r2-bake-upload', 'r2-patch', 'r2-delete'}.issubset(observed):
        return False
    if record.get('connected_inventory_properties_transport') != 'ais':
        return False
    property_edits = [request.get('request', {}).get('desc') for request in requests
        if request.get('path') == 'r2-patch' and request.get('item') == '71000000-0000-0000-0000-000000000002']
    if property_edits != ['Native legacy metadata edit', 'Native modern metadata edit']:
        return False
    if directory is None:
        return False
    try:
        root = Path(directory) / 'r2'
        if contracts['gltf_topology_cases'] != list(range(9)) or contracts['hud_attachment_points'] != list(range(31,39)):
            return False
        if contracts['remote_body_parts'] != list(range(8)):
            return False
        catalog = json.loads((root/'motion-catalog.json').read_text())
        expected_motion_labels = {'stand','female-old-walk','female-old-run','female-new-walk','female-new-run',
            'fall-standup','ground-sit','crouch','crouch-walk','turn-left','turn-right','female-sit','male-sit',
            'male-old-walk','male-old-run','male-new-walk','male-new-run','female-emote-smile'}
        expected_motion_labels.update('emote-express_'+name for name in ('afraid_emote','anger_emote','bored_emote','cry_emote','disdain',
            'embarrassed_emote','frown','kiss','laugh_emote','open_mouth','repulsed_emote','sad_emote','shrug_emote','smile','surprise_emote','tongue_out',
            'toothsmile','wink_emote','worry_emote'))
        motion_rows = contracts['specialized_motion_cases']
        if (motion_rows != catalog['cases'] or len(motion_rows) != 37 or
                {row['label'] for row in motion_rows} != expected_motion_labels):
            return False
        fetched = {request.get('asset') for request in requests if request.get('path') == 'r2-asset'}
        if not {row['mapped'] for row in motion_rows if row['kind'] != 'emote'}.issubset(fetched):
            return False
        if len(contracts['physics_weights']) != 3 or not all(math.isfinite(value) for value in contracts['physics_weights']):
            return False
        retired = contracts['retired_scene']
        if retired['owners'] or retired['neighbours'] or retired['owned_object_slots'] or retired['owned_avatar_slots']:
            return False
        if retired['assets']['requests'] or int.from_bytes(base64.b64decode(retired['assets']['resident_bytes'],validate=True),'big'):
            return False
        capture = read_llsd(ET.fromstring((root/'bake-readbacks.xml').read_text()))
        readbacks = capture['bakes']
        # Three 72-byte vertices (UV1/tangent/color included) plus three U32 indices.
        # Keep this independent of the viewer-reported allocation/readback sizes.
        if capture['mesh_replacement'] != dict(budget_bytes=228, refusals=2, input_rejections=2, gpu_buffer_readbacks=2,
                prior_generation_retained=True, owner_release_allows_retry=True,
                bytes_after_retire=0, snapshot_bytes_after_retire=0):
            return False
        comparisons = contracts['bake_codec_comparisons']
        if len(readbacks) != 11 or len(comparisons) != 11:
            return False
        if {row['index'] for row in readbacks} != set(range(11)) or {row['index'] for row in readbacks} != {row['index'] for row in comparisons}:
            return False
        if not body_lods_passed(contracts['body_lod_publications']):
            return False
        artifacts = []
        for row in readbacks:
            width,height = row['width'],row['height']
            if (row['passed'] is not True or row['row_order'] != 'top-first' or row['channels'] != 'RGBA' or
                    not 0 < width <= 2048 or not 0 < height <= 2048 or row['bytes'] != width*height*4):
                return False
            names = [root/f"bake-{row['index']}-{kind}.rgba" for kind in ('actual','expected')]
            if any(path.stat().st_size != row['bytes'] for path in names):
                return False
            digests = []
            for path in names:
                with path.open('rb') as stream:
                    digest = hashlib.file_digest(stream,'sha256').hexdigest()
                digests.append(digest)
                artifacts.append(dict(path=path.name,bytes=path.stat().st_size,sha256=digest))
            if digests[0] != digests[1]:
                return False
        for kind, size in (('vertices', 216), ('indices', 12)):
            hashes = []
            for side in ('actual', 'expected'):
                path = root/f'mesh-{kind}-{side}.bin'
                if path.stat().st_size != size:
                    return False
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                hashes.append(digest)
                artifacts.append(dict(path=path.name, bytes=size, sha256=digest))
            if hashes[0] != hashes[1]:
                return False
        for row in comparisons:
            if (row['maximum_allowed_error'] != 16 or row['maximum_allowed_mean_error'] != 2 or
                    not 0 <= row['maximum_error'] <= 16 or not math.isfinite(row['mean_error']) or not 0 <= row['mean_error'] <= 2):
                return False
        uploads = {row['asset']:row for row in requests if row.get('path') == 'r2-bake-upload'}
        for row in contracts['uploaded_appearance']['bake_uploads']:
            identifier = str(UUID(row['asset']))
            filename = root/(identifier+'.j2c')
            receipt = uploads[identifier]
            with filename.open('rb') as stream:
                digest = hashlib.file_digest(stream,'sha256').hexdigest()
            if filename.stat().st_size != receipt['bytes'] or digest != receipt['sha256']:
                return False
            artifacts.append(dict(path=filename.name,bytes=filename.stat().st_size,sha256=digest))
        legacy = contracts['legacy_uploads']
        if not legacy:
            return False
        for row in legacy:
            filename = root/(str(UUID(row['asset']))+'.legacy.j2c')
            if row['packets'] <= 0 or filename.stat().st_size != row['bytes'] or row['bytes'] <= 0:
                return False
            with filename.open('rb') as stream:
                digest = hashlib.file_digest(stream,'sha256').hexdigest()
            artifacts.append(dict(path=filename.name,bytes=filename.stat().st_size,sha256=digest))
        saved = read_llsd(ET.fromstring((root/'contracts.xml').read_text()))
        if saved != contracts:
            return False
        manifest = dict(artifacts=artifacts)
        if write_manifest:
            (root/'artifact-hashes.json').write_text(json.dumps(manifest,indent=2)+'\n')
        elif json.loads((root/'artifact-hashes.json').read_text()) != manifest:
            return False
    except (KeyError,TypeError,ValueError,OSError,ET.ParseError):
        return False
    return True

INVENTORY_ROOT = '70000000-0000-0000-0000-000000000001'
LIBRARY_ROOT = '72000000-0000-0000-0000-000000000001'
LIBRARY_OWNER = '72000000-0000-0000-0000-000000000002'
SYSTEM_FOLDERS = [(0, 'Textures', 3), (7, 'Notecards', 2), (1, 'Sounds', 4),
                  (2, 'Calling Cards', 5), (3, 'Landmarks', 6), (5, 'Clothing', 7),
                  (6, 'Objects', 8), (10, 'Scripts', 9), (13, 'Body Parts', 10),
                  (14, 'Trash', 11), (15, 'Photo Album', 12), (16, 'Lost And Found', 13),
                  (20, 'Animations', 14), (21, 'Gestures', 15), (23, 'Favorites', 16),
                  (46, 'Current Outfit', 17), (48, 'My Outfits', 18), (50, 'Received Items', 19),
                  (56, 'Settings', 20), (57, 'Materials', 21)]
INVENTORY_SKELETON = [dict(folder_id=INVENTORY_ROOT,
                          parent_id='00000000-0000-0000-0000-000000000000',
                          name='My Inventory', type_default=8, version=1)] + [
    dict(folder_id=f'70000000-0000-0000-0000-{suffix:012d}', parent_id=INVENTORY_ROOT,
         name=name, type_default=kind, version=1) for kind, name, suffix in SYSTEM_FOLDERS]



def baseline_inventory_item(identifier):
    """Known received texture and script, shared by AIS and legacy item fetches."""
    script = identifier == '71000000-0000-0000-0000-000000000004'
    if not script and identifier != '71000000-0000-0000-0000-000000000001':
        return None
    agent = '10000000-0000-0000-0000-000000000001'
    return dict(item_id=identifier,
        parent_id='70000000-0000-0000-0000-000000000009' if script else '70000000-0000-0000-0000-000000000003',
        asset_id='60000000-0000-0000-0000-000000000009' if script else '60000000-0000-0000-0000-000000000006',
        type='lsltext' if script else 'texture', inv_type='lsl' if script else 'texture',
        name='Native script' if script else 'Native received texture', desc='Loopback inventory asset', flags=0, created_at=0,
        permissions=dict(creator_id=agent, owner_id=agent, last_owner_id=agent, base_mask=2147483647,
                         owner_mask=2147483647, next_owner_mask=2147483647, group_mask=0, everyone_mask=0),
        sale_info=dict(sale_type='not', sale_price=0))


def baseline_inventory_items(identifier):
    return [dict(item_id='71000000-0000-0000-0000-000000000020',
                    parent_id=identifier, asset_id='60000000-0000-0000-0000-000000000020',
                    type='landmark', inv_type='landmark', flags=1, name='Native favorite', desc='Replay connected landmark',
                    created_at=1, permissions=dict(base_mask=2147483647, owner_mask=2147483647,
                    group_mask=0, everyone_mask=0, next_owner_mask=2147483647,
                    creator_id='10000000-0000-0000-0000-000000000001', owner_id='10000000-0000-0000-0000-000000000001',
                    last_owner_id='10000000-0000-0000-0000-000000000001', group_id='00000000-0000-0000-0000-000000000000',
                    is_owner_group=False), sale_info=dict(sale_type=0, sale_price=0))] if identifier == '70000000-0000-0000-0000-000000000016' else []


def append_llsd(parent, value):
    if value is None:
        ET.SubElement(parent, 'undef')
    elif isinstance(value, dict):
        result = ET.SubElement(parent, 'map')
        for key, item in value.items():
            ET.SubElement(result, 'key').text = key
            append_llsd(result, item)
    elif isinstance(value, list):
        result = ET.SubElement(parent, 'array')
        for item in value:
            append_llsd(result, item)
    elif isinstance(value, bool):
        ET.SubElement(parent, 'boolean').text = str(value).lower()
    elif isinstance(value, datetime):
        ET.SubElement(parent, 'date').text = value.isoformat().replace('+00:00', 'Z')
    elif isinstance(value, UUID):
        ET.SubElement(parent, 'uuid').text = str(value)
    elif isinstance(value, int):
        ET.SubElement(parent, 'integer').text = str(value)
    elif isinstance(value, float):
        ET.SubElement(parent, 'real').text = repr(value)
    else:
        ET.SubElement(parent, 'string').text = str(value)

DEFAULT_ENVIRONMENT_ASSET = '5646d39e-d3d7-6aff-ed71-30fc87d64a91'

from vulkan_environment_evidence import environment_pixels_passed

REQUIRED = ('connected_script_debug_controls', 'connected_script_icon_gpu', 'connected_script_icon_pick', 'connected_script_icon_retirement', 'connected_region_authorized_update', 'connected_region_readonly_update', 'connected_region_nine_tabs', 'connected_region_encoded_model', 'connected_topobjects_encoded_beacon', 'connected_console_http', 'connected_region_ui_retirement', 'connected_my_environments_filters', 'connected_fixed_environment_editors', 'connected_environment_gpu_consumption', 'connected_parcel_request_echo', 'connected_parcel_rectangle_pixels', 'connected_environment_capability_edit', 'connected_about_land_all_tabs', 'connected_environment_nested_dialogs', 'connected_land_media_url_cancel', 'connected_default_environment_asset', 'connected_land_buy_policy_cancel', 'connected_land_sell_stale_intent_cancel', 'connected_utility_floaters', 'connected_keyboard_first_person_strafe', 'connected_keyboard_window_dispatch', 'connected_camera_controls', 'connected_land_permissions', 'connected_native_statistics', 'connected_script_notification_controls', 'connected_script_dialog_encoded', 'connected_script_textbox_encoded', 'connected_conference_remap_retirement', 'connected_favorites_modal_acknowledgement', 'connected_notification_console', 'connected_account_context_actions', 'connected_nearby_frontend', 'connected_text_context_menu', 'connected_favorites_context', 'connected_native_cursor', 'connected_native_chrome', 'connected_toolbar_customization', 'connected_inventory_main', 'connected_toolbar_chat', 'im_nearby_forwarding', 'connected_window_pause_resume', 'connected_skin_geometry', 'connected_account_benefits', 'connected_inventory_script_edit_save', 'connected_inventory_properties', 'connected_inventory_sound_preview',
            'connected_inventory_texture_upload', 'connected_inventory_sound_upload',
            'connected_group_titles', 'connected_group_directory_search',
            'connected_inventory_share', 'connected_resident_pay', 'connected_payment_balance_gate',
            'connected_payment_insufficient_funds_shared_currency',
            'connected_display_name_update',
            'connected_profile_second_life_image_save', 'connected_profile_first_life_image_save',
            'connected_resident_profile_notes', 'connected_group_profile_members_roles',
            'connected_preferences_apply_cancel', 'preferences_repeated_tabs', 'preferences_graphics_persistence', 'connected_contact_set_edit', 'connected_blocklist_edit',
            'connected_transcript_preview', 'im_inventory_offer', 'im_group_attachment', 'im_inventory_preview',
            'connected_ui_image_decoded', 'im_friendship_notification', 'im_friend_status_history', 'im_group_notice', 'im_group_invitation_decline_encoded',
            'im_contacts_ui', 'im_readback', 'im_direct_encoded', 'im_direct_incoming_offline', 'im_typing', 'im_mute',
            'im_group_start', 'im_group_encoded', 'im_group_incoming', 'im_group_participants',
            'im_group_leave', 'im_group_reopen', 'im_group_invitation', 'im_group_start_error',
            'im_group_event_error', 'im_group_force_close', 'im_callback_expired',
            'login_authentication', 'login_challenge', 'login_account_setup', 'notification_storage', 'malformed', 'unknown', 'wrong_host', 'udp_chat', 'http_gate', 'udp_gate',
            'queued_expired', 'relogin', 'chat_submit', 'typing', 'settings_gate', 'readback',
            'partial_init_cleanup', 'connection_timeout', 'encoded_chat', 'encoded_typing', 'ui_gate', 'logout_timeout', 'native_people_account_ui', 'native_navigation_request', 'native_navigation_transition', 'native_navigation_epoch', 'native_navigation_identity', 'native_navigation_local', 'native_location_lookup', 'native_landmark_v2', 'encoded_channels', 'queued_http_chat', 'mute_request', 'mute_transfer_cleanup', 'input_history', 'status_messages', 'connected_timeout', 'reliable_failure')


def configure_replay_window_environment(env, *, headless, windows, require_media):
    if not headless:
        env.pop('VS_VULKAN_DIAGNOSTIC_HEADLESS', None)
        return
    env['VS_VULKAN_DIAGNOSTIC_HEADLESS'] = '1'
    env.pop('WAYLAND_DISPLAY', None)
    if windows:
        env.pop('DISPLAY', None)
    elif env.get('DISPLAY'):
        # The native viewer uses its explicit offscreen window implementation.
        # CEF's offscreen browser still needs the X11 service supplied by Xvfb.
        env['XDG_SESSION_TYPE'] = 'x11'
    elif require_media:
        raise ValueError('Headless CEF qualification requires X11; run under xvfb-run')


def configure_replay_contract_environment(env,args):
    flags={
        'VS_VULKAN_REPLAY_LOCAL_IMAGES':args.local_image_contracts,
        'VS_VULKAN_REPLAY_TEXTURE_CACHE':args.texture_cache_contracts,
        'VS_VULKAN_REPLAY_TEXTURE_CACHE_READ_ONLY':args.texture_cache_read_only,
        'VS_VULKAN_REPLAY_R1':args.r1_contracts,
        'VS_VULKAN_REPLAY_R2':args.r2_contracts,
        'VS_VULKAN_REPLAY_TERRAIN':args.terrain_contracts,
        'VS_VULKAN_REPLAY_PENDING_CACHE_WRITE_RESTART':args.pending_cache_write_restart,
    }
    for key,enabled in flags.items():
        if enabled:env[key]='1'
        else:env.pop(key,None)


def r3_terrain_contracts_passed(report,directory,requests):
    try:
        record=report['r3_terrain']
        if any(record.get(key) is not True for key in ('complete','live_scale_transform','paint_bake','paint_deleted')):
            return False
        if record.get('paint_bit_depth')!=1 or len(record['layers'])!=4:
            return False
        override=record['regional_override']
        relative='r3/terrain/regional-override.rgba'
        path=Path(directory)/relative
        if (override.get('file')!=relative or override.get('pixels_verified') is not True or
                override.get('base_factor')!=.25 or path.is_symlink() or
                not path.resolve().is_relative_to(Path(directory).resolve())):return False
        data=path.read_bytes()
        if len(data)!=64*64*4 or any(abs(value-(137,0,0,255)[offset%4])>1 for offset,value in enumerate(data)):return False
        if not any(row.get('path')=='/r3-terrain-overrides' and row.get('method')=='GET' and
                   row.get('base_factor')==.25 for row in requests):return False
        colors=((255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,255,255))
        for index,row in enumerate(record['layers']):
            relative=f'r3/terrain/paint-layer-{index}.rgba'
            if row.get('layer')!=index or row.get('file')!=relative or row.get('pixels_verified') is not True:
                return False
            path=Path(directory)/relative
            if path.is_symlink() or not path.resolve().is_relative_to(Path(directory).resolve()):
                return False
            data=path.read_bytes()
            if len(data)!=64*64*4 or any(abs(value-colors[index][offset%4])>1 for offset,value in enumerate(data)):
                return False
        return True
    except (KeyError,TypeError,ValueError,OSError):
        return False


def parcel_rectangle_contracts_passed(report, directory):
    try:
        row = report['parcel_rectangle']
        if (row.get('file') != 'parcel/rectangle.rgba' or row.get('width') != 64 or
                row.get('height') != 64 or row.get('pixels_verified') is not True):
            return False
        root = Path(directory).resolve()
        path = Path(directory) / 'parcel/rectangle.rgba'
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            return False
        pixels = path.read_bytes()
        if len(pixels) != 64 * 64 * 4:
            return False
        if row.get('wall_samples_verified') != 3:
            return False
        for x, y, color in ((25,32,(255,92,163,255)),
                            (32,32,(255,51,204,255)),
                            (32,31,(255,125,130,255))):
            index = (y * 64 + x) * 4
            if any(abs(pixels[index + channel] - expected) > 1
                   for channel, expected in enumerate(color)):
                return False
        posts = sum(pixels[index:index + 3] == bytes((255, 255, 0))
                    for index in range(0, len(pixels), 4))
        return posts > 0 and row.get('opaque_post_pixels') == posts
    except (KeyError, TypeError, ValueError, OSError):
        return False


def hud_icon_contracts_passed(record, directory):
    """Require the actual submitted icon's complete immutable RGBA evidence."""
    try:
        row = record['script_icon_readback']
        width, height = row['width'], row['height']
        if type(width) is not int or type(height) is not int or not 2 < width <= 1024 or not 2 < height <= 1024:
            return False
        if row['pixels_verified'] is not True or row['mismatched_components'] != 0:
            return False
        generation = base64.b64decode(row['image_generation'], validate=True)
        if len(generation) != 8 or int.from_bytes(generation, 'big') == 0:
            return False
        root = Path(directory).resolve()
        actual_path = (root / row['actual']).resolve()
        expected_path = (root / row['expected']).resolve()
        if not actual_path.is_relative_to(root) or not expected_path.is_relative_to(root) or actual_path == expected_path:
            return False
        metadata = read_llsd(ET.parse(root / 'script-icon/readback.xml').getroot())
        if metadata != row:
            return False
        actual, expected = actual_path.read_bytes(), expected_path.read_bytes()
        if len(actual) != width * height * 4 or len(expected) != len(actual):
            return False
        if any(abs(a - b) > 3 for a, b in zip(actual, expected)):
            return False
        # An empty/transparent draw must not pass by reproducing the clear color.
        visible = sum(expected[index:index + 3] != b'\xff\x00\xff' for index in range(0, len(expected), 4))
        return visible >= max(4, width * height // 16)
    except (KeyError, TypeError, ValueError, OSError, ET.ParseError):
        return False


def terrain_contracts_passed(report,directory):
    try:
        terrain=report['terrain_publication']
        if terrain.get('complete') is not True:return False
        for phase,color in (('pending',(127,127,127,255)),('published',(32,160,64,255)),('retired',(127,127,127,255))):
            row=terrain[phase]
            if row.get('file')!='terrain/'+phase+'.rgba' or row.get('draw_packets')!=1 or row.get('pixels_verified') is not True:return False
            pixels=(Path(directory)/row['file']).read_bytes()
            if len(pixels)!=64*64*4 or any(abs(value-color[index%4])>1 for index,value in enumerate(pixels)):return False
        return (Path(directory)/'terrain/detail.j2c').is_file()
    except (KeyError,TypeError,ValueError,OSError):return False


def read_llsd(element):
    if element.tag == 'llsd':
        return read_llsd(list(element)[0])
    if element.tag == 'map':
        children = list(element)
        if len(children) % 2 or any(children[i].tag != 'key' for i in range(0, len(children), 2)):
            raise ValueError('Malformed LLSD evidence map')
        return {children[i].text: read_llsd(children[i + 1]) for i in range(0, len(children), 2)}
    if element.tag == 'array':
        return [read_llsd(item) for item in element]
    if element.tag == 'boolean':
        return element.text in ('true', '1')
    if element.tag == 'integer':
        return int(element.text or '0')
    if element.tag == 'real':
        return float(element.text or '0')
    return element.text or ''


def assess(record, returncode, log):
    return (returncode == 0 and record.get('schema') == 1
            and record.get('mode') == 'viewer-native-session-replay'
            and record.get('passed') is True and record.get('world_owners') == 0
            and record.get('live_server_qualified') is False
            and all(record.get(key) is True for key in REQUIRED)
            and record.get('sent', 0) >= 4 and record.get('expired', 0) >= 1
            and 'Native Vulkan normal login shut down' in log
            and not any(text in log for text in ('Validation Error', 'VUID-', 'DILIGENT 2:',
                                                 'Forbidden GL path', 'Rejected native widget type:', 'Failed to create dummy',
                                                 'Making dummy ', 'is not a valid child', 'Could not create widget', "Failed to assign '", 'VIEWER TIMEOUT'))
            and not re.search(r'with no handler function received: \w+Reply\b', log))


def isolated_profile_environment(environment, evidence, windows, shared_profile=None):
    """Use existing LLDir overrides only in the spawned viewer environment."""
    evidence = Path(evidence).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    if shared_profile is None:
        profile = Path(tempfile.mkdtemp(prefix='profile-', dir=evidence)).resolve()
    else:
        profile = Path(shared_profile).resolve()
        if profile.parent != evidence.parent or not profile.name.startswith('profile-'):
            raise ValueError('Shared replay profiles must be dedicated siblings of their evidence')
        marker = profile / 'native-replay-profile.json'
        identity = dict(schema=1, credential_free_native_replay=True)
        if profile.exists():
            if not marker.is_file() or json.loads(marker.read_text()) != identity:
                raise ValueError('Refusing to reuse an ordinary viewer profile')
        else:
            profile.mkdir()
            marker.write_text(json.dumps(identity) + '\n')
    result = dict(environment)
    if windows:
        for name, child in [('APPDATA', 'roaming'), ('LOCALAPPDATA', 'local')]:
            target = profile / child
            target.mkdir(exist_ok=True)
            result[name] = str(target)
    else:
        target = profile / 'application'
        target.mkdir(exist_ok=True)
        # APP_NAME is Vulkanstorm; ADDRESS_SIZE64 initAppDirs appends _x64.
        result['VULKANSTORM_X64_USER_DIR'] = str(target)
        result['VULKANSTORM_USER_DIR'] = str(target)
    (profile / 'cache').mkdir(exist_ok=True)
    return result, profile


def favorites_fetch_observed(requests):
    """Require account favorites to come through the real descendants service."""
    legacy = any(record.get('path') == '/inventory' and any(
        folder.get('folder_id') == '70000000-0000-0000-0000-000000000016'
        and folder.get('fetch_items') is True
        for folder in record.get('request', {}).get('folders', [])) for record in requests)
    ais = any(record.get('path') == 'r2-ais-category' and any(
        category.get('category') == '70000000-0000-0000-0000-000000000016'
        and '71000000-0000-0000-0000-000000000020' in category.get('items', [])
        for category in record.get('categories', [])) for record in requests)
    return legacy or ais


def run_staged_viewer(command, stage, env):
    """A finite process deadline kills a stalled viewer and preserves its output."""
    debugger = None
    if platform.system() == 'Linux' and env.get('VS_VULKAN_REPLAY_GDB') == '1':
        debugger = shutil.which('gdb')
        if not debugger:
            raise RuntimeError('Linux connected CI requires GDB for native crash backtraces')
        command = [debugger, '--batch', '--nx', '--return-child-result',
                   '-ex', 'set pagination off', '-ex', 'set print thread-events off',
                   '-ex', 'handle SIGPIPE SIGUSR2 SIG64 nostop noprint pass',
                   '-ex', 'run', '-ex', 'thread apply all bt', '--args', *command]
    launch = run_headless_viewer_process if env.get('VS_VULKAN_DIAGNOSTIC_HEADLESS') == '1' else run_viewer_process
    result = launch(command, cwd=stage, env=env, timeout=PROCESS_REPLAY_SECONDS)
    result.native_debugger = 'gdb' if debugger else None
    return result


def require_no_child_process_crashes(log):
    """A surviving viewer cannot qualify after one of its media services crashed."""
    retiring = set()
    for line in log.splitlines():
        if '#LLProcess#' not in line:
            continue
        child = re.search(r'\bSLPlugin(?:\.exe)?\s*\((\d+)\)', line, re.IGNORECASE)
        if not child:
            continue
        pid = child.group(1)
        if re.search(r'\bkilling\s+SLPlugin(?:\.exe)?\s*\(', line, re.IGNORECASE):
            retiring.add(pid)
            continue
        if 'killed with exception ' in line:
            raise RuntimeError('Media plugin crashed: ' + line.strip())
        fault = re.search(r'killed by signal (\d+)\b', line)
        # LLProcess::kill requests SIGTERM. Only its preceding same-child log
        # proves intentional retirement; an OOM/SIGKILL is never a success.
        if fault and (int(fault.group(1)) != 15 or pid not in retiring):
            raise RuntimeError('Media plugin crashed: ' + line.strip())
        exited = re.search(r'exited with code (-?\d+)\b', line)
        if exited and int(exited.group(1)) != 0 and not (int(exited.group(1)) == -1 and pid in retiring):
            raise RuntimeError('Media plugin failed: ' + line.strip())


def read_session_evidence(directory, returncode):
    """Report the viewer's failure before attempting to read success evidence."""
    if returncode != 0:
        detail = ''
        failure_path = directory / 'r2/failure.xml'
        if failure_path.is_file():
            try:
                failure = read_llsd(ET.parse(failure_path).getroot())
                detail = f'; R2 step {failure.get("step", "unknown")}: {failure.get("failure", "unknown failure")}'
            except (OSError, ValueError, ET.ParseError):
                detail = f'; unreadable failure evidence: {failure_path}'
        raise RuntimeError(f'Viewer exited with code {returncode}{detail}')
    return read_llsd(ET.parse(directory / 'session-replay.xml').getroot())


def prepare_native_debugger():
    """Install the CI-only debugger once, before spawning the skin cases."""
    if platform.system() == 'Linux' and os.environ.get('VS_VULKAN_REPLAY_GDB') == '1' and not shutil.which('gdb'):
        if os.environ.get('GITHUB_ACTIONS') != 'true':
            raise RuntimeError('Install GDB before requesting a local native backtrace')
        subprocess.run(['sudo', 'apt-get', '-o', 'Acquire::Retries=3', 'install', '-y', 'gdb'],
                       check=True, timeout=120)
        if not shutil.which('gdb'):
            raise RuntimeError('GDB installation did not supply the Linux native debugger')


def skin_matrix_passed(results, planned):
    return bool(planned) and set(results) == set(planned) and all(record['passed'] for record in results.values())


def conference_start_observed(requests):
    """Require the real capability start with exactly the selected recipient."""
    starts = [record['request'] for record in requests
              if record.get('path') == '/chat' and isinstance(record.get('request'), dict)
              and record['request'].get('method') == 'start conference']
    if len(starts) != 1 or starts[0].get('params') != ['50000000-0000-0000-0000-000000000005']:
        return False
    try:
        temporary = UUID(starts[0].get('session-id', ''))
    except (ValueError, TypeError, AttributeError):
        return False
    return temporary.int != 0 and temporary != UUID('84000000-0000-0000-0000-000000000008')


def default_environment_asset_observed(requests, fixture):
    if not fixture.is_file():
        return False
    data = fixture.read_bytes()
    return bool(data) and any(
        row.get('path', '').rstrip('/') == '/asset' and
        row.get('query', {}).get('settings_id') == [DEFAULT_ENVIRONMENT_ASSET] and
        row.get('bytes') == len(data) and row.get('sha256') == hashlib.sha256(data).hexdigest()
        for row in requests)


def landstat_event(payload):
    """Preserve LLSD wire types when the simulator fixture returns its report."""
    def mapping(element):
        if element.tag!='map':raise ValueError('Expected LLSD map')
        children=list(element)
        if len(children)%2 or any(children[i].tag!='key' for i in range(0,len(children),2)):raise ValueError('Malformed LLSD map')
        result={children[i].text:children[i+1] for i in range(0,len(children),2)}
        if len(result)*2!=len(children):raise ValueError('Duplicate LLSD map key')
        return result
    def u32(element):
        if element.tag!='binary':raise ValueError('U32 must retain LLSD binary wire type')
        data=base64.b64decode(element.text or '',validate=True)
        if len(data)!=4:raise ValueError('Invalid U32 wire size')
        return int.from_bytes(data,'big')
    root=ET.fromstring(payload)
    if root.tag!='llsd' or len(root)!=1:raise ValueError('Invalid LLSD envelope')
    envelope=mapping(root[0])
    if set(envelope)!={'message','body'} or envelope['message'].tag!='string' or envelope['message'].text!='LandStatReply':raise ValueError('Wrong report envelope')
    body=mapping(envelope['body'])
    if set(body)!={'RequestData','ReportData'}:raise ValueError('Unexpected report blocks')
    if any(element.tag!='array' or len(element)!=1 for element in body.values()):raise ValueError('Expected one report row')
    request=mapping(body['RequestData'][0]);row=mapping(body['ReportData'][0])
    if set(request)!={'ReportType','RequestFlags','TotalObjectCount'} or [u32(request[key]) for key in ('ReportType','RequestFlags','TotalObjectCount')]!=[0,0x80000000,1]:raise ValueError('Incorrect report request metadata')
    if set(row)!={'TaskLocalID','TaskID','LocationX','LocationY','LocationZ','Score','TaskName','OwnerName'}:raise ValueError('Incomplete actual report row')
    if u32(row['TaskLocalID'])!=123 or row['TaskID'].tag!='uuid' or row['TaskID'].text!='50000000-0000-0000-0000-000000000005':raise ValueError('Wrong actual report identity')
    for key in ('LocationX','LocationY','LocationZ','Score'):
        if row[key].tag!='real' or not math.isfinite(float(row[key].text or 'nan')):raise ValueError('Invalid actual report coordinate/score')
    if float(row['Score'].text)!=.25:raise ValueError('Wrong actual report score')
    for key,value in (('TaskName','Native tracked CPU target'),('OwnerName','CPU Sender')):
        if row[key].tag!='string' or row[key].text!=value:raise ValueError('Wrong actual report text')
    # Body bytes retain binary U32 and UUID types. Rebuilding the parsed evidence
    # dictionary through append_llsd would silently turn U32 binaries into strings.
    event=ET.Element('map');ET.SubElement(event,'key').text='message';ET.SubElement(event,'string').text='LandStatReply';ET.SubElement(event,'key').text='body';event.append(envelope['body'])
    return ET.tostring(event,encoding='utf-8')


class SimulatorHTTP(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self,account_id='10000000-0000-0000-0000-000000000001',console_async=False):
        self.account_id=account_id
        self.console_async=console_async
        super().__init__(('127.0.0.1', 0), Handler)
        self.auth_requests = 0
        self.seed_requests = 0
        self.event_requests = 0
        self.region_events=[]
        self.region_event_lock=threading.Lock()
        self.requests = []
        self.profile_updates = {}
        self.upload_requests = {}
        self.upload_directory = None
        self.replay_finished = threading.Event()

    @property
    def url(self):
        return 'http://127.0.0.1:' + str(self.server_address[1])


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
            # Viewer shutdown closes outstanding event-poll HTTP connections.
            pass

    def log_message(self, *args):
        pass

    def account_payload(self,data):
        # All synthetic inventory/appearance fixtures identify the same owner.
        # Remap that UUID together when qualifying a distinct cache consumer.
        return data.replace(b'10000000-0000-0000-0000-000000000001',self.server.account_id.encode('ascii'))

    def respond_llsd(self, body, status=200):
        root = ET.Element('llsd'); append_llsd(root, body)
        data = self.account_payload(ET.tostring(root, encoding='utf-8'))
        self.send_response(status)
        self.send_header('Content-Type', 'application/llsd+xml')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urlsplit(self.path).path
        if ui_environment_fixture.get(self,path,parse_qs(urlsplit(self.path).query)):return
        if path == '/region-experiences':
            self.server.requests.append(dict(path=path,method='GET'))
            self.respond_llsd(dict(allowed=[],blocked=[],trusted=[]));return
        if path in ('/r3-media','/r3-media-phase') and getattr(self.server,'r3_directory',None):
            phase_file=self.server.r3_directory/'media-phase.txt'
            phase=2 if phase_file.exists() and phase_file.read_text().strip()=='2' else 1
            payload=MEDIA_PAGE if path=='/r3-media' else str(phase).encode()
            self.server.requests.append(dict(path=path,method='GET',phase=phase,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
            self.send_response(200);self.send_header('Content-Type','text/html' if path=='/r3-media' else 'text/plain')
            self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(payload)))
            self.end_headers();self.wfile.write(payload);return
        if path=='/r3-terrain-overrides' and getattr(self.server,'r3_directory',None):
            self.server.requests.append(dict(path=path,method='GET',base_factor=.25))
            self.respond_llsd(dict(success=True,overrides=[dict(bc=[.25,.25,.25,1.]) for _ in range(4)]))
            return
        if path=='/r3-legacy-materials'  and getattr(self.server,'r3_directory',None):
            source=self.server.r3_directory/'legacy-materials.xml'
            if not source.is_file() or source.stat().st_size>2*1024*1024:self.send_error(404);return
            payload=source.read_bytes()
            self.server.requests.append(dict(path=path,method='GET',bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
            self.send_response(200);self.send_header('Content-Type','application/llsd+xml')
            self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if r2_fixture.get(self,path,parse_qs(urlsplit(self.path).query)):return
        if path in ('/r1-mesh', '/r2-mesh') and getattr(self.server,'r1_directory',None):
            if parse_qs(urlsplit(self.path).query).get('mesh_id') != ['a1000000-0000-0000-0000-000000000001']:
                self.send_error(404); return
            directory=self.server.r1_directory
            marker=directory/'mesh-generation.txt'
            revision=2 if marker.exists() and marker.read_text().strip()=='2' else 1
            data=(directory/f'mesh-{revision}.bin').read_bytes()
            self.server.requests.append({'path':'/r1-mesh','revision':revision,'bytes':len(data)})
            self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
        terrain=getattr(self.server,'terrain_fixture',None)
        if terrain and parse_qs(urlsplit(self.path).query).get('texture_id')==['c3800000-0000-0000-0000-000000000001']:
            deadline=time.monotonic()+60
            while not terrain.is_file() and time.monotonic()<deadline:time.sleep(.02)
            if not terrain.is_file():self.send_error(503);return
            data=terrain.read_bytes()
            self.server.requests.append(dict(path=path,method='GET',query={'texture_id':['c3800000-0000-0000-0000-000000000001']},texture_id='c3800000-0000-0000-0000-000000000001',terrain=True,bytes=len(data)))
            self.send_response(200);self.send_header('Content-Type','image/jp2');self.send_header('Content-Length',str(len(data)))
            self.end_headers();self.wfile.write(data);return
        if path.rstrip('/') == '/asset':
            query = parse_qs(urlsplit(self.path).query)
            if query.get('texture_id'):
                if (getattr(self.server,'r3_directory',None) and getattr(self.server,'stream_texture',None) and
                        query['texture_id']==['a3000000-0000-0000-0000-000000000003']):
                    self.server.requests.append(dict(path=path,method='GET',query=query,bytes=0,shutdown_pending=True))
                    (self.server.r3_directory/'pending-cache-read.requested').write_text('HTTP request observed; response intentionally pending\n')
                    self.server.replay_finished.wait(25)
                    self.close_connection=True
                    return
                if getattr(self.server,'r3_directory',None):
                    ids=self.server.r3_directory/'terrain/regional-material-ids.txt'
                    if ids.is_file() and query['texture_id'][0] in ids.read_text().splitlines():
                        self.server.requests.append(dict(path=path,method='GET',query=query,bytes=0,status=404))
                        self.send_response(404);self.send_header('Content-Length','0');self.end_headers();return
                stream=getattr(self.server,'stream_texture',None)
                streaming=bool(stream and query['texture_id'] in
                    (['a3000000-0000-0000-0000-000000000001'],['a3000000-0000-0000-0000-000000000002']))
                fixture=stream if streaming else getattr(self.server,'texture_fixture',None)
                data=(fixture or (Path(__file__).resolve().parents[1]/'indra/newview/skins/default/textures/transparent.j2c')).read_bytes()
                if streaming and self.headers.get('Range'):
                    import re
                    match=re.fullmatch(r'bytes=(\d+)-(\d*)',self.headers['Range'])
                    if not match:self.send_error(400);return
                    start=int(match[1]);end=min(int(match[2]) if match[2] else len(data)-1,len(data)-1)
                    if start> end or start>=len(data):
                        self.send_response(416);self.send_header('Content-Range',f'bytes */{len(data)}')
                        self.send_header('Content-Length','0');self.end_headers();return
                    payload=data[start:end+1]
                    self.server.requests.append(dict(path=path,method='GET',query=query,bytes=len(payload),
                        range_start=start,range_end=end,total_bytes=len(data)))
                    self.send_response(206);self.send_header('Content-Type','image/jp2')
                    self.send_header('Content-Range',f'bytes {start}-{end}/{len(data)}')
                    self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
            elif query.get('settings_id'):
                fixture = getattr(self.server, 'environment_fixture', None)
                if (query['settings_id'] != [DEFAULT_ENVIRONMENT_ASSET] or not fixture or
                        not fixture.is_file() or fixture.stat().st_size > 2 * 1024 * 1024):
                    self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers(); return
                data = fixture.read_bytes()
            elif query.get('material_id') and getattr(self.server,'r3_directory',None):
                identities=[f'b3100000-0000-0000-0000-{index:012d}' for index in range(1,5)]
                if len(query['material_id'])!=1 or query['material_id'][0] not in identities:
                    self.send_error(404);return
                fixture=self.server.r3_directory/'remote-materials'/(query['material_id'][0]+'.asset')
                if not fixture.is_file() or fixture.stat().st_size>2*1024*1024:
                    self.send_error(404);return
                data=fixture.read_bytes()
                if query['material_id']==[identities[3]] and not any(item.get('query',{}).get('material_id')==[identities[3]] for item in self.server.requests):
                    data=(fixture.parent/(identities[1]+'.asset')).read_bytes()
            elif query.get('landmark_id') == ['60000000-0000-0000-0000-000000000020']:
                data = b'Landmark version 2\nregion_id 40000000-0000-0000-0000-000000000004\nlocal_pos 128 128 25\n'
            elif query.get('lsltext_id') == ['60000000-0000-0000-0000-000000000009']:
                data = b'default { state_entry() { llOwnerSay("Native initial script"); } }'
            elif (query.get('sound_id') == ['60000000-0000-0000-0000-000000000008']
                  and self.server.upload_directory is not None
                  and (self.server.upload_directory / 'sound.ogg').is_file()):
                data = (self.server.upload_directory / 'sound.ogg').read_bytes()
            else:
                self.send_error(404)
                return
            receipt=dict(path=path, method='GET', query=query, bytes=len(data))
            if query.get('material_id') or query.get('settings_id'):receipt['sha256']=hashlib.sha256(data).hexdigest()
            self.server.requests.append(receipt)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if path.startswith('/profile/'):
            agent = path.removeprefix('/profile/')
            self.server.requests.append({'path': path, 'method': 'GET'})
            profile = dict(id=agent, sl_image_id='60000000-0000-0000-0000-000000000006',
                fl_image_id='60000000-0000-0000-0000-000000000006', partner_id='00000000-0000-0000-0000-000000000000',
                sl_about_text='Native resident profile', fl_about_text='Native first life',
                member_since=datetime(2020, 1, 1, tzinfo=timezone.utc), hide_age=False, customer_type='Resident',
                notes='Native profile notes', online=True, allow_publish=False, identified=False, transacted=False,
                caption='Native test resident', groups=[dict(id='80000000-0000-0000-0000-000000000008',
                    name='Native replay group', image_id='60000000-0000-0000-0000-000000000006')], picks=[])
            if agent == self.server.account_id:
                profile['sl_image_id'] = profile['fl_image_id'] = '00000000-0000-0000-0000-000000000000'
            profile.update(self.server.profile_updates.get(agent, {}))
            self.respond_llsd(profile)
            return
        if path == '/avatar-picker/':
            self.server.requests.append({'path': path, 'method': 'GET'})
            self.respond_llsd(dict(agents=[dict(id='50000000-0000-0000-0000-000000000005',
                username='cpu.sender', display_name='CPU Sender', legacy_first_name='CPU', legacy_last_name='Sender',
                is_display_name_default=True)]))
            return
        if urlsplit(self.path).path == '/user-info':
            self.server.requests.append({'path': '/user-info', 'method': 'GET'})
            root = ET.Element('llsd')
            append_llsd(root, dict(success=True, email='fixture@example.invalid',
                                  directory_visibility='default', im_via_email=False))
            data = self.account_payload(ET.tostring(root, encoding='utf-8'))
            self.send_response(200)
            self.send_header('Content-Type', 'application/llsd+xml')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if urlsplit(self.path).path == '/names/':
            ids = parse_qs(urlsplit(self.path).query).get('ids', [])
            self.server.requests.append({'path': '/names/', 'ids': ids})
            root = ET.Element('llsd')
            append_llsd(root, {'agents': [dict(id=agent, username='cpu.sender',
                         display_name='CPU Sender', legacy_first_name='CPU', legacy_last_name='Sender',
                         is_display_name_default=True) for agent in ids], 'bad_ids': []})
            data = self.account_payload(ET.tostring(root, encoding='utf-8'))
            self.send_response(200)
            self.send_header('Content-Type', 'application/llsd+xml')
            self.send_header('Cache-Control', 'max-age=3600')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if urlsplit(self.path).path != '/texture':
            self.send_error(404)
            return
        # Existing lossless CPU skin asset provides reproducible image data.
        data = (Path(__file__).resolve().parents[1] / 'indra/newview/skins/default/textures/transparent.j2c').read_bytes()
        self.server.requests.append({'path': '/texture', 'texture_id': parse_qs(urlsplit(self.path).query).get('texture_id', [])})
        self.send_response(200)
        self.send_header('Content-Type', 'image/jp2')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_DELETE(self):
        path=urlsplit(self.path).path
        if ui_environment_fixture.modify(self,'DELETE',path,parse_qs(urlsplit(self.path).query)):return
        if r2_fixture.modify(self,'DELETE',urlsplit(self.path).path):return
        self.send_error(404)

    def do_PATCH(self):
        size=int(self.headers.get('Content-Length','0'))
        if not 0<size<=65536:self.send_error(400);return
        request=read_llsd(ET.fromstring(self.rfile.read(size)))
        if r2_fixture.modify(self,'PATCH',urlsplit(self.path).path,request):return
        self.send_error(404)

    def do_PUT(self):
        if urlsplit(self.path).path==ui_environment_fixture.ROUTE:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=65536:self.send_error(400);return
            try:request=ui_environment_fixture.decode(ET.fromstring(self.rfile.read(size)))
            except (ValueError,ET.ParseError):self.send_error(400);return
            ui_environment_fixture.modify(self,'PUT',ui_environment_fixture.ROUTE,parse_qs(urlsplit(self.path).query),request)
            return
        path = urlsplit(self.path).path
        size = int(self.headers.get('Content-Length', '0'))
        if path != '/profile/'+self.server.account_id or not 0 < size <= 65536:
            self.send_error(400)
            return
        request = read_llsd(ET.fromstring(self.rfile.read(size)))
        if not isinstance(request, dict) or not request or not set(request).issubset(
                {'sl_image_id', 'fl_image_id', 'notes', 'sl_about_text', 'fl_about_text', 'allow_publish', 'hide_age'}):
            self.send_error(400)
            return
        self.server.requests.append({'path': path, 'method': 'PUT', 'body': request})
        self.server.profile_updates.setdefault(path.removeprefix('/profile/'), {}).update(request)
        self.respond_llsd({'success': True})

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        path = urlsplit(self.path).path
        limit = 64 * 1024 * 1024 if path == '/r2-bake-upload' else 65536
        if size < 0 or size > limit:
            self.send_error(413)
            return
        payload = self.rfile.read(size)
        path = urlsplit(self.path).path
        if path == '/script-data':
            expected = b'default { state_entry() { llOwnerSay("Native edited script"); }}'
            metadata = self.server.upload_requests.get('script')
            if not metadata or payload.rstrip(b'\0') != expected:
                self.send_error(400)
                return
            if self.server.upload_directory:
                self.server.upload_directory.mkdir(parents=True, exist_ok=True)
                (self.server.upload_directory / 'script.lsl').write_bytes(payload)
            self.server.requests.append(dict(path=path, method='POST', bytes=len(payload),
                sha256=hashlib.sha256(payload).hexdigest(), metadata=metadata))
            self.respond_llsd(dict(state='complete', new_asset='60000000-0000-0000-0000-000000000010',
                compiled=True, errors=[]))
            return
        if path.startswith('/upload-data/'):
            kind = path.removeprefix('/upload-data/')
            metadata = self.server.upload_requests.get(kind)
            valid_signature = (kind == 'texture' and payload.startswith(bytes.fromhex('ff4fff51'))) or (
                kind == 'sound' and payload.startswith(b'OggS') and b'vorbis' in payload[:128])
            if not metadata or not valid_signature:
                # The bounded request body has been consumed. Return a fully
                # framed error on this connection; send_error closes the socket
                # and can reset a Windows client before it reads the error body.
                self.respond_llsd(dict(state='error', message='Missing upload handshake or invalid encoded asset'), status=400)
                return
            if self.server.upload_directory:
                self.server.upload_directory.mkdir(parents=True, exist_ok=True)
                (self.server.upload_directory / (kind + ('.j2c' if kind == 'texture' else '.ogg'))).write_bytes(payload)
            self.server.requests.append(dict(path=path, method='POST', bytes=len(payload),
                                             sha256=hashlib.sha256(payload).hexdigest(),
                                             signature=payload[:16].hex(), metadata=metadata))
            suffix = '002' if kind == 'texture' else '003'
            asset_suffix = '007' if kind == 'texture' else '008'
            r2_fixture.uploaded_item(self.server, '71000000-0000-0000-0000-000000000' + suffix,
                                     '60000000-0000-0000-0000-000000000' + asset_suffix, metadata)
            self.respond_llsd(dict(state='complete', new_inventory_item='71000000-0000-0000-0000-000000000' + suffix,
                                   new_asset='60000000-0000-0000-0000-000000000' + asset_suffix,
                                   upload_price=0, new_next_owner_mask=2147483647,
                                   new_group_mask=0, new_everyone_mask=0))
            return
        if path == '/login':
            params, method = xmlrpc.client.loads(payload)
            if method != 'login_to_simulator' or len(params) != 1:
                self.send_error(400)
                return
            query = parse_qs(urlsplit(self.path).query)
            port = int(query['sim_port'][0])
            if not 0 < port < 65536:
                self.send_error(400)
                return
            self.server.auth_requests += 1
            # Keep fixture credentials/tokens out of archived request evidence.
            self.server.requests.append({'path': '/login', 'method': method,
                                         'attempt': self.server.auth_requests})
            response = dict(login='true',
                            agent_id='10000000-0000-0000-0000-000000000001',
                            session_id='20000000-0000-0000-0000-000000000002',
                            secure_session_id='70000000-0000-0000-0000-000000000007',
                            first_name='Native', last_name='Replay',
                            sim_ip='127.0.0.1', sim_port=port,
                            region_x=256, region_y=512, circuit_code=123456,
                            seed_capability=self.server.url + '/seed', message='Loopback login',
                            account_type='Basic',
                            account_level_benefits=dict(animated_object_limit=2, animation_upload_cost=0,
                                attachment_limit=38, create_group_cost=0, group_membership_limit=42,
                                picks_limit=10, sound_upload_cost=0, texture_upload_cost=0),
                            premium_packages={name: dict(benefits=dict(animated_object_limit=2,
                                animation_upload_cost=0, attachment_limit=38, create_group_cost=0,
                                group_membership_limit=42, picks_limit=10, sound_upload_cost=0,
                                texture_upload_cost=0)) for name in ('Base', 'Premium')},
                            **{'inventory-root': [{'folder_id': INVENTORY_ROOT}],
                               'inventory-skeleton': INVENTORY_SKELETON,
                               'inventory-lib-root': [{'folder_id': LIBRARY_ROOT}],
                               'inventory-lib-owner': [{'agent_id': LIBRARY_OWNER}],
                               'inventory-skel-lib': [dict(folder_id=LIBRARY_ROOT,
                                   parent_id='00000000-0000-0000-0000-000000000000',
                                   name='Library', type_default=8, version=1)],
                               'buddy-list': [{'buddy_id': '50000000-0000-0000-0000-000000000005',
                                               'buddy_rights_given': 1, 'buddy_rights_has': 1}]})
            if self.server.auth_requests == 1 or params[0].get('read_critical') not in (True, 1):
                response = dict(login='false', reason='critical', message='Loopback authentication policy acknowledgement')
            data = self.account_payload(xmlrpc.client.dumps((response,), methodresponse=True).encode('utf-8'))
            self.send_response(200)
            self.send_header('Content-Type', 'text/xml')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if r2_fixture.binary_post(self,path,payload):return
        request = read_llsd(ET.fromstring(payload))
        self.server.requests.append({'path': self.path, 'request': request})
        if r2_fixture.post(self,path,request):return
        if path == '/ui-sim-console-async':
            if request != 'native qualification':self.send_error(400);return
            event=ET.Element('map');ET.SubElement(event,'key').text='message';ET.SubElement(event,'string').text='SimConsoleResponse';ET.SubElement(event,'key').text='body';ET.SubElement(event,'string').text='native console accepted'
            with self.server.region_event_lock:self.server.region_events.append(ET.tostring(event,encoding='utf-8'))
            self.respond_llsd({});return
        if path == '/ui-sim-console':
            if request != 'native qualification':self.send_error(400);return
            self.respond_llsd('native console accepted');return
        if path == '/ui-open-region-settings':
            if not isinstance(request,dict):self.send_error(400);return
            self.respond_llsd({'success':True});return
        if path == '/metadata':
            if request.get('fields') != ['experience'] or not request.get('item-id'):
                self.send_error(400)
                return
            # This fixture's inventory scripts have no associated experience.
            self.respond_llsd({})
            return
        if path == '/script-request':
            if request.get('item_id') != '71000000-0000-0000-0000-000000000004' or request.get('target') not in ('mono', 'lsl2'):
                self.send_error(400)
                return
            self.server.upload_requests['script'] = request
            self.respond_llsd(dict(state='upload', uploader=self.server.url + '/script-data'))
            return
        if path == '/upload-request':
            kind = request.get('asset_type')
            expected_name = 'Native uploaded ' + str(kind)
            if kind not in ('texture', 'sound') or request.get('inventory_type') != kind or (
                    request.get('folder_id') != '70000000-0000-0000-0000-000000000003') or (
                    request.get('name') != expected_name) or not isinstance(request.get('expected_upload_cost'), int) or (
                    request['expected_upload_cost'] < 0):
                self.send_error(400)
                return
            self.server.upload_requests[kind] = request
            self.respond_llsd(dict(state='upload', uploader=self.server.url + '/upload-data/' + kind))
            return
        if self.path == '/remote-parcel':
            if not isinstance(request,dict) or not {'region_id','location'}.issubset(request) or not set(request).issubset({'region_id','location','region_handle'}):
                self.send_error(400);return
            try:
                region=UUID(request['region_id']);location=request['location']
                if not region.int or not isinstance(location,list) or len(location)!=3 or not all(isinstance(v,(int,float)) and math.isfinite(v) for v in location):raise ValueError('Invalid parcel location')
            except (TypeError,ValueError,AttributeError):self.send_error(400);return
            self.server.requests.append(dict(path=self.path,method='POST',request=request))
            self.respond_llsd(dict(parcel_id=uuid5(NAMESPACE_URL,str(region)+'/parcel/31')));return
        if self.path == '/seed':
            self.server.seed_requests += 1
            if not {'EventQueueGet', 'GetDisplayNames', 'ChatSessionRequest', 'GetTexture', 'AvatarPickerSearch'}.issubset(request):
                self.send_error(400)
                return
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'GetDisplayNames'
            ET.SubElement(mapping, 'string').text = self.server.url + '/names/'
            ET.SubElement(mapping, 'key').text = 'UntrustedSimulatorMessage'
            ET.SubElement(mapping, 'string').text = self.server.url + '/message'
            for name, route in [('UserInfo', '/user-info'), ('UpdateAgentInformation', '/agent-info'),
                                ('UpdateAgentLanguage', '/agent-language'), ('AgentProfile', '/profile'),
                                ('AvatarPickerSearch', '/avatar-picker'),
                                ('UploadAgentProfileImage', '/profile-upload'),
                                ('NewFileAgentInventory', '/upload-request'),
                                ('UpdateScriptAgent', '/script-request'), ('ViewerAsset', '/asset'),
                                ('GetMetadata', '/metadata'), ('RemoteParcelRequest', '/remote-parcel'),
                                ('DispatchOpenRegionSettings', '/ui-open-region-settings'),
                                (('SimConsoleAsync' if self.server.console_async else 'SimConsole'), ('/ui-sim-console-async' if self.server.console_async else '/ui-sim-console')),
                                ('RegionExperiences', '/region-experiences'),
                                ('ExtEnvironment', '/ui-environment')]:
                ET.SubElement(mapping, 'key').text = name
                ET.SubElement(mapping, 'string').text = self.server.url + route
            if getattr(self.server,'r3_directory',None):
                ET.SubElement(mapping,'key').text='RenderMaterials'
                ET.SubElement(mapping,'string').text=self.server.url+'/r3-legacy-materials'
                ET.SubElement(mapping,'key').text='ModifyRegion'
                ET.SubElement(mapping,'string').text=self.server.url+'/r3-terrain-overrides'
            if getattr(self.server,'r2_directory',None):
                ET.SubElement(mapping,'key').text='GetMesh2'
                ET.SubElement(mapping,'string').text=self.server.url+'/r2-mesh'
            elif getattr(self.server,'r1_directory',None):
                ET.SubElement(mapping,'key').text='GetMesh2'
                ET.SubElement(mapping,'string').text=self.server.url+'/r1-mesh'
            ET.SubElement(mapping, 'key').text = 'EventQueueGet'
            ET.SubElement(mapping, 'string').text = self.server.url + '/events'
            ET.SubElement(mapping, 'key').text = 'FetchInventory2'
            ET.SubElement(mapping, 'string').text = self.server.url + '/items'
            ET.SubElement(mapping, 'key').text = 'FetchInventoryDescendents2'
            ET.SubElement(mapping, 'string').text = self.server.url + '/inventory'
            if getattr(self.server,'r2_directory',None):
                ET.SubElement(mapping,'key').text='InventoryAPIv3'
                ET.SubElement(mapping,'string').text=self.server.url+'/r2-ais'
                ET.SubElement(mapping,'key').text='UploadBakedTexture'
                ET.SubElement(mapping,'string').text=self.server.url+'/r2-bake'
            ET.SubElement(mapping, 'key').text = 'GetTexture'
            ET.SubElement(mapping, 'string').text = self.server.url + '/texture'
            ET.SubElement(mapping, 'key').text = 'ChatSessionRequest'
            ET.SubElement(mapping, 'string').text = self.server.url + '/chat'
        elif self.path == '/items':
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'agent_id'
            ET.SubElement(mapping, 'uuid').text = '10000000-0000-0000-0000-000000000001'
            ET.SubElement(mapping, 'key').text = 'items'
            items = ET.SubElement(mapping, 'array')
            for item in request.get('items', []):
                fixture=r2_fixture.item(self.server,item.get('item_id'))
                if fixture is not None:
                    append_llsd(items,fixture);continue
                baseline = baseline_inventory_item(item['item_id'])
                if baseline is not None:
                    append_llsd(items, baseline)
        elif self.path == '/inventory':
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'folders'
            folders = ET.SubElement(mapping, 'array')
            for folder in request.get('folders', []):
                children = [dict(folder_id=item['folder_id'], parent_id=item['parent_id'],
                                 name=item['name'], type=item['type_default'], version=1)
                            for item in getattr(self.server,'r2_categories',INVENTORY_SKELETON)
                            if item['parent_id'] == folder['folder_id']]
                result = ET.SubElement(folders, 'map')
                for key, tag, value in [('folder_id', 'uuid', folder['folder_id']),
                                        ('owner_id', 'uuid', folder['owner_id']),
                                        ('version', 'integer', '1'), ('descendents', 'integer', str(len(children)))]:
                    ET.SubElement(result, 'key').text = key; ET.SubElement(result, tag).text = value
                ET.SubElement(result, 'key').text = 'categories'; append_llsd(result, children)
                favorites = folder['folder_id'] == '70000000-0000-0000-0000-000000000016'
                landmark_items = baseline_inventory_items(folder['folder_id'])
                if favorites:
                    for idx, element in enumerate(list(result)):
                        if element.tag == 'key' and element.text == 'descendents': list(result)[idx+1].text = str(len(children) + len(landmark_items))
                fixture=r2_fixture.folder(self.server,folder['folder_id'])
                if fixture is not None:
                    landmark_items,version=fixture
                    for idx,element in enumerate(list(result)):
                        if element.tag=='key' and element.text=='version':list(result)[idx+1].text=str(version)
                        if element.tag=='key' and element.text=='descendents':list(result)[idx+1].text=str(len(children)+len(landmark_items))
                ET.SubElement(result, 'key').text = 'items'; append_llsd(result, landmark_items)
        elif self.path == '/message':
            if request.get('message')=='LandStatReply':
                try:event=landstat_event(payload)
                except(ValueError,TypeError,KeyError,ET.ParseError):self.send_error(400);return
                with self.server.region_event_lock:self.server.region_events.append(event)
            root = ET.Element('llsd'); append_llsd(root, {})
        elif self.path in ('/user-info', '/agent-info', '/agent-language'):
            root = ET.Element('llsd'); append_llsd(root, dict(request, success=True))
        elif self.path == '/chat':
            root = ET.Element('llsd')
            if request.get('method') == 'fetch history':
                ET.SubElement(root, 'array')
            else:
                mapping = ET.SubElement(root, 'map')
                ET.SubElement(mapping, 'key').text = 'agents'
                agents = ET.SubElement(mapping, 'array')
                for agent in ('10000000-0000-0000-0000-000000000001', '50000000-0000-0000-0000-000000000005'):
                    ET.SubElement(agents, 'uuid').text = agent
                ET.SubElement(mapping, 'key').text = 'session_info'
                ET.SubElement(mapping, 'map')
        elif self.path == '/events':
            self.server.event_requests += 1
            time.sleep(.1)
            root = ET.Element('llsd'); mapping = ET.SubElement(root, 'map')
            ET.SubElement(mapping, 'key').text = 'id'
            ET.SubElement(mapping, 'integer').text = str(self.server.event_requests)
            ET.SubElement(mapping, 'key').text = 'events'
            events = ET.SubElement(mapping, 'array')
            with self.server.region_event_lock:
                queued=self.server.region_events
                self.server.region_events=[]
            for event in queued:events.append(ET.fromstring(event))
            if self.server.event_requests == 1:
                event = ET.SubElement(events, 'map')
                ET.SubElement(event, 'key').text = 'message'
                ET.SubElement(event, 'string').text = 'ObjectUpdate'
                ET.SubElement(event, 'key').text = 'body'
                ET.SubElement(event, 'map')
            if self.server.event_requests >= 1:
                event = ET.SubElement(events, 'map')
                ET.SubElement(event, 'key').text = 'message'
                ET.SubElement(event, 'string').text = 'ChatFromSimulator'
                ET.SubElement(event, 'key').text = 'body'
                body = ET.SubElement(event, 'map')
                ET.SubElement(body, 'key').text = 'ChatData'
                row = ET.SubElement(ET.SubElement(body, 'array'), 'map')
                for name, tag, value in (
                    ('FromName', 'string', 'HTTP sender'),
                    ('SourceID', 'uuid', '60000000-0000-0000-0000-000000000006'),
                    ('OwnerID', 'uuid', '60000000-0000-0000-0000-000000000006'),
                    ('SourceType', 'integer', '1'), ('ChatType', 'integer', '1'),
                    ('Audible', 'integer', '1'), ('Message', 'string', 'Native event queue chat')):
                    ET.SubElement(row, 'key').text = name
                    ET.SubElement(row, tag).text = value
                ET.SubElement(row, 'key').text = 'Position'; append_llsd(row, [128.0, 128.0, 25.0])
        else:
            self.send_error(404)
            return
        data = self.account_payload(ET.tostring(root, encoding='utf-8'))
        try:
            self.send_response(200)
            self.send_header('Content-Type', 'application/llsd+xml')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # The production owner cancels suspended HTTP during reset.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--harness-evidence', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--register-windows-manifests', action='store_true')
    parser.add_argument('--skin', default='default')
    parser.add_argument('--theme', default='')
    parser.add_argument('--language', default='en')
    parser.add_argument('--ui-scale', type=float, default=1.0,
                        help='Viewer UI scale used for connected skin/readback qualification')
    parser.add_argument('--console-async',action='store_true',help='Advertise actual SimConsoleAsync instead of deprecated synchronous fallback in the isolated simulator fixture')
    parser.add_argument('--all-skins', action='store_true', help='Run the complete connected replay for every packaged skin/theme and German overlay')
    parser.add_argument('--headless', action='store_true', help='Replay production viewer integration offscreen without a native window; does not qualify presentation')
    parser.add_argument('--cache-directory', type=Path, help='Explicit isolated cache shared by sequential qualification processes')
    parser.add_argument('--cache-account',choices=('primary','secondary'),default='primary',help='Synthetic owner identity for cache transition qualification')
    parser.add_argument('--cache-grid',choices=('agni','aditi'),default='agni',help='Built-in grid identity; authentication stays on the isolated loopback fixture')
    parser.add_argument('--cache-policy', type=Path, help='Prior process cache-version metadata for restart qualification')
    parser.add_argument('--cache-size', type=int, help='Texture cache size in MiB for qualification')
    parser.add_argument('--cache-clear', action='store_true', help='Exercise the normal next-startup cache purge setting')
    parser.add_argument('--texture-cache-contracts', action='store_true', help='Exercise native streaming/preferences and archive GPU image readbacks')
    parser.add_argument('--pending-cache-write-restart',action='store_true',help='Verify the prior process shutdown write through normal fetch/cache and GPU publication')
    parser.add_argument('--local-image-contracts','--r3-image-contracts',dest='local_image_contracts',action='store_true',help='Exercise production R3 local/remote material, image, bump, replacement and retirement contracts')
    parser.add_argument('--texture-cache-read-only', action='store_true', help='Exercise the native sibling texture-cache policy in an isolated headless replay')
    parser.add_argument('--profile-directory', type=Path, help='Dedicated shared credential-free profile for concurrent headless viewer qualification')
    parser.add_argument('--completion-gate', type=Path, help='Retain the headless viewer marker lock after replay until this file exists')
    parser.add_argument('--texture-fixture', type=Path, help='Nontransparent local J2C fixture for ordinary replay image requests')
    parser.add_argument('--r1-contracts', action='store_true', help='Replay R1 owner, region, resource and lifecycle contracts through production viewer code')
    parser.add_argument('--terrain-contracts',action='store_true',help='Qualify actual terrain geometry during pending, published and retired detail textures')
    parser.add_argument('--r2-contracts',action='store_true',help='Replay production R2 geometry and connected appearance actions')
    parser.add_argument('--native-backtrace', action='store_true', help='Run Linux replay under GDB; ordinary qualification runs directly')
    args = parser.parse_args()
    # Terrain materials must exercise the viewer's received terrain geometry,
    # rather than qualifying only the standalone shader charts.
    args.terrain_contracts = args.terrain_contracts or args.local_image_contracts
    if args.pending_cache_write_restart and not (args.headless and args.texture_cache_contracts):
        parser.error('Pending-write restart requires isolated headless cache qualification')
    if args.cache_size is not None and not 256 <= args.cache_size <= 100*1024:
        parser.error('--cache-size must be between 256 and 102400 MiB')
    if args.texture_cache_read_only and not (args.headless and args.texture_cache_contracts):
        parser.error('Read-only cache injection requires isolated headless cache qualification')
    if args.texture_fixture and (not args.texture_fixture.is_file() or args.texture_fixture.stat().st_size>16*1024*1024):
        parser.error('Expected a bounded local texture fixture')
    started = time.monotonic()
    if not .75 <= args.ui_scale <= 4:
        parser.error('--ui-scale must be between 0.75 and 4')
    windows = platform.system() == 'Windows'
    if args.native_backtrace:
        if platform.system() != 'Linux':
            parser.error('--native-backtrace is supported on Linux only')
        os.environ['VS_VULKAN_REPLAY_GDB'] = '1'
    prepare_native_debugger()
    build = args.build_directory.resolve()
    stage = build / 'newview' / ('RelWithDebInfo' if windows else 'packaged')
    if args.all_skins:
        directory = args.evidence.resolve()
        directory.mkdir(parents=True, exist_ok=True)
        results = {}
        cases = {name: (*selection, 1.0) for name, selection in skin_cases(stage).items()}
        cases['skin-ansastorm_modern-125percent'] = ('ansastorm_modern', '', 'en', 1.25)
        cases['skin-ansastorm_modern-150percent'] = ('ansastorm_modern', '', 'en', 1.5)
        print(f'Connected UI qualification: {len(cases)} cases, timeout {CASE_REPLAY_SECONDS}s per case', flush=True)
        for index, (name, (skin, theme, language, ui_scale)) in enumerate(cases.items(), 1):
            case_started = time.monotonic()
            print(f'RUN: connected UI {name} [{index}/{len(cases)}] '
                  f'(skin={skin}, theme={theme or "base"}, language={language}, scale={ui_scale}; '
                  f'timeout {CASE_REPLAY_SECONDS}s)', flush=True)
            child = directory / name
            command = [sys.executable, str(Path(__file__).resolve()),
                       '--build-directory', str(build), '--runtime', str(args.runtime.resolve()),
                       '--harness-evidence', str(args.harness_evidence.resolve()), '--evidence', str(child),
                       '--skin', skin, '--theme', theme, '--language', language,
                       '--ui-scale', str(ui_scale)]
            if args.register_windows_manifests:
                command.append('--register-windows-manifests')
            if args.console_async:command.append('--console-async')
            if args.native_backtrace:
                command.append('--native-backtrace')
            if args.headless:
                command.append('--headless')
            if args.r1_contracts:
                command.append('--r1-contracts')
            if args.r2_contracts:command.append('--r2-contracts')
            try:
                code = subprocess.run(command, timeout=CASE_REPLAY_SECONDS).returncode
                record = json.loads((child / 'results.json').read_text())
                results[name] = dict(passed=code == 0 and record.get('passed') is True,
                                     exit_code=code, evidence=str(child))
            except (subprocess.TimeoutExpired, OSError, ValueError) as error:
                results[name] = dict(passed=False, error=str(error), evidence=str(child))
            status = 'PASS' if results[name]['passed'] else 'FAIL'
            print(f'{status}: connected UI {name} ({time.monotonic() - case_started:.1f}s); '
                  f'log: {child / "viewer.log"}', flush=True)
            if not results[name]['passed']:
                print(f'FAIL: connected UI case {name}: {results[name]}', flush=True)
                break  # Preserve the first actionable failure instead of repeating it across all skins.
        complete = len(results) == len(cases)
        passed = skin_matrix_passed(results, cases)
        (directory / 'results.json').write_text(json.dumps(dict(passed=passed, cases=results,
                                                               planned_cases=list(cases), complete=complete,
                                                               live_server_qualified=False), indent=2) + '\n')
        print(('PASS' if passed else 'FAIL') + f': connected UI skin matrix '
              f'({len(results)}/{len(cases)} cases completed, {time.monotonic() - started:.1f}s)', flush=True)
        return 0 if passed else 1
    case_label = (f'connected-session replay (skin={args.skin}, theme={args.theme or "base"}, '
                  f'language={args.language}, scale={args.ui_scale})')
    print(f'RUN: viewer {case_label} (timeout {PROCESS_REPLAY_SECONDS}s)', flush=True)
    binaries = list(stage.glob('*-bin.exe')) if windows else list((stage / 'bin').glob('*-bin'))
    if len(binaries) != 1 or 'VS_VULKAN_DIAGNOSTICS:BOOL=ON' not in (build / 'CMakeCache.txt').read_text():
        raise RuntimeError('Expected one diagnostic-enabled fully staged viewer')
    executable = binaries[0]
    staging = json.loads((build / 'newview/vulkan-ci-staging.json').read_text())
    if staging['files'].get(str(executable.relative_to(stage))) != hashlib.sha256(executable.read_bytes()).hexdigest():
        raise RuntimeError('Viewer staging checksum differs')
    runtime = json.loads(args.runtime.read_text())
    root = Path(__file__).resolve().parents[1]
    if runtime['driver'] not in ('swiftshader', 'lavapipe') or runtime['sources'] != json.loads((root / 'scripts/vulkan_ci_dependencies.json').read_text()):
        raise RuntimeError('Runtime pins differ from the software CI lock')
    for name, digest in runtime['staged_sha256'].items():
        if hashlib.sha256((args.runtime.resolve().parent / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Runtime checksum mismatch: ' + name)
    harness = json.loads(args.harness_evidence.read_text())
    if not harness_prerequisite_passed(harness, args.headless):
        raise RuntimeError('Missing prerequisite native graphics evidence for the selected replay mode')
    directory = args.evidence.resolve(); directory.mkdir(parents=True, exist_ok=True)
    if args.local_image_contracts:r3_gltf_fixture.write(directory/'r3/gltf-images')
    for name in ('session-replay.xml', 'session-chat.ppm', 'session-chat-expected.ppm', 'session-chat-expected-products.ppm', 'session-chat-packets.txt', 'session-chat-scene.ppm', 'session-chat-scene.rgba',
                 'im/session-chat.ppm', 'im/session-chat-expected.ppm', 'im/session-chat-expected-products.ppm', 'im/session-chat-packets.txt', 'im/session-chat-scene.ppm', 'im/session-chat-scene.rgba', 'results.json'):
        (directory / name).unlink(missing_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('VS_VULKAN_DIAGNOSTIC')}
    if (args.profile_directory or args.completion_gate) and not args.headless:
        raise ValueError('Concurrent-instance controls require a headless replay')
    env, isolated_profile = isolated_profile_environment(env, directory, windows, args.profile_directory)
    if args.completion_gate:
        gate = args.completion_gate.resolve()
        if gate.parent != directory.parent or gate.exists():
            raise ValueError('Completion gate must be a fresh sibling of the replay evidence')
        env['VS_VULKAN_REPLAY_COMPLETION_GATE'] = str(gate)
    else:
        env.pop('VS_VULKAN_REPLAY_COMPLETION_GATE', None)
    (directory / 'profile-isolation.json').write_text(json.dumps(dict(profile=str(isolated_profile),
        platform='windows' if windows else 'linux'), indent=2) + '\n')
    for key in ('VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES', 'VK_ADD_LAYER_PATH', 'VK_INSTANCE_LAYERS',
                'VK_LOADER_DRIVERS_SELECT', 'VK_LOADER_DRIVERS_DISABLE', 'VK_LAYER_ENABLES', 'VK_LAYER_DISABLES'):
        env.pop(key, None)
    if not windows:
        env['SDL_VIDEODRIVER'] = 'x11'
        env['LD_LIBRARY_PATH'] = str(stage / 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    env.update(VK_DRIVER_FILES=runtime['icd'], VK_LAYER_PATH=runtime['layer_path'],
               VK_LOADER_LAYERS_DISABLE='~implicit~', VK_LOADER_DEBUG='error,warn,driver,layer',
               VS_VULKAN_DIAGNOSTIC_REPLAY=str(directory))
    configure_replay_window_environment(env, headless=args.headless, windows=windows,
                                        require_media=args.local_image_contracts)
    configure_replay_contract_environment(env,args)
    layer = directory / 'vk_layer_settings.txt'; layer.write_text('khronos_validation.validate_sync = true\n')
    env['VK_LAYER_SETTINGS_PATH'] = str(layer)
    command = [str(executable), '--settings', 'vs_native_session_ci.xml', '--set', 'ClientSettingsFile',
               str(directory / 'settings.xml'), '--set', 'RenderBackend', 'Vulkan', '--set', 'AutoLogin', 'false',
               '--set', 'QuitAfterSeconds', str(VIEWER_REPLAY_SECONDS), '--set', 'RenderDebugGLSession', 'true',
               '--set', 'UpdaterShowReleaseNotes', '0', '--set', 'FSShowWhitelistReminder', 'false']
    selection = ET.Element('llsd')
    selection_values = {name: dict(Type='String', Persist=0, Comment='Replay skin selection', Value=value)
                        for name,value in [('SkinCurrent',args.skin),('SkinCurrentTheme',args.theme),('Language',args.language)]}
    if args.cache_policy:
        policy=json.loads(args.cache_policy.read_text())
        rules={'LocalCacheVersion':('S32',int),'DiskCacheVersion':('S32',int),'LastJ2CVersion':('String',str)}
        if set(policy)!=set(rules) or any(type(policy[key]) is not value_type for key,(_,value_type) in rules.items()):
            raise RuntimeError('Invalid restart cache policy metadata')
        selection_values.update({key:dict(Type=kind,Persist=0,Comment='Isolated cache restart policy',Value=policy[key])
                                 for key,(kind,_) in rules.items()})
    append_llsd(selection,selection_values)
    selection_file = directory / 'ui-selection.xml'
    selection_file.write_bytes(ET.tostring(selection, encoding='utf-8'))
    # Empty theme strings cannot survive the viewer's command-line tokenizer.
    command.extend(['--sessionsettings', str(selection_file)])
    command.extend(['--set', 'UIScaleFactor', str(args.ui_scale)])
    command.extend(['--set','CmdLineGridChoice','util.'+args.cache_grid+'.lindenlab.com'])
    if args.profile_directory:
        command.extend(['--set', 'AllowMultipleViewers', 'true'])
    cache_directory=args.cache_directory.resolve() if args.cache_directory else isolated_profile/'cache'
    cache_directory.mkdir(parents=True,exist_ok=True)
    command.extend(['--set', 'CacheLocation', str(cache_directory),
                    '--set', 'NewCacheLocation', str(cache_directory),
                    '--set', 'FSUseNearbyChatConsole', 'true'])
    if args.cache_size is not None:command.extend(['--set','CacheSize',str(args.cache_size)])
    if args.cache_clear:command.extend(['--set','PurgeCacheOnNextStartup','true'])
    if args.texture_cache_contracts:command.extend(['--set','RenderCompressTextures','false',
        '--set','RenderMaxTextureResolution','2048','--set','TextureLoadFullRes','false'])
    report = {}; log = ''; code = None; passed = False
    account_id='10000000-0000-0000-0000-00000000000'+('1' if args.cache_account=='primary' else '2')
    with SimulatorHTTP(account_id,console_async=args.console_async) as server:
        env['VS_VULKAN_REPLAY_ACCOUNT_ID']=account_id
        server.texture_fixture=args.texture_fixture.resolve() if args.texture_fixture else None
        server.terrain_fixture=directory/'terrain/detail.j2c' if args.terrain_contracts else None
        server.r3_directory=directory/'r3' if args.local_image_contracts else None
        server.stream_texture=directory/'r3/texture.j2c' if args.texture_cache_contracts else None
        server.r1_directory=directory/'r1' if args.r1_contracts else None
        r2_fixture.configure(server,directory/'r2' if args.r2_contracts else None)
        server.r2_categories = [dict(row) for row in INVENTORY_SKELETON]
        server.r2_baseline_items = baseline_inventory_items
        server.r2_baseline_item = baseline_inventory_item
        server.upload_directory = directory / 'upload-bodies'
        server.environment_fixture = directory / 'default-environment.llsd'
        ui_environment_fixture.configure(server,read_llsd)
        worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
        env['VS_VULKAN_REPLAY_SEED'] = server.url + '/seed'
        env['VS_VULKAN_REPLAY_LOGIN'] = server.url + '/login'
        try:
            with windows_manifest_registration(runtime, args.register_windows_manifests):
                process = run_staged_viewer(command, stage, env)
            server.replay_finished.set()
            code = process.returncode; log = process.stdout.decode('utf-8', errors='replace')
            require_no_child_process_crashes(log)
            report = read_session_evidence(directory, code)
            report['process_isolation'] = process_isolation_evidence(process)
            report['native_debugger'] = getattr(process, 'native_debugger', None)
            passed = assess(report, code, log)
            if not hud_icon_contracts_passed(report,directory):
                raise RuntimeError('Missing or corrupt actual script icon GPU readback evidence')
            if not ui_environment_fixture.exchange_observed(server.requests):
                raise RuntimeError('Missing actual selected-parcel ExtEnvironment GET/PUT request and accepted reply')
            region_edits=[row.get('request') for row in server.requests if row.get('path') == '/ui-open-region-settings']
            if len(region_edits)!=2 or any(not isinstance(row,dict) or not isinstance(row.get('enable_teen_mode'),bool) for row in region_edits) or {row.get('enforce_max_build') for row in region_edits}!={False,True}:
                raise RuntimeError('Missing actual shared Aurora Apply/restore HTTP transactions')
            console_route='/ui-sim-console-async' if args.console_async else '/ui-sim-console'
            if report.get('console_api')!=('async' if args.console_async else 'sync'):
                raise RuntimeError('Viewer used unexpected simulator console API')
            if not any(row.get('path') == console_route and row.get('request') == 'native qualification' for row in server.requests):
                raise RuntimeError('Missing actual shared simulator-console HTTP request')
            if not default_environment_asset_observed(server.requests, server.environment_fixture):
                raise RuntimeError('Missing real shared default environment asset HTTP acquisition')
            if args.r1_contracts and not r1_contracts_passed(report,server.requests):
                raise RuntimeError('Missing production R1 ownership/lifecycle or actual mesh replacement evidence')
            if args.terrain_contracts and not terrain_contracts_passed(report,directory):
                raise RuntimeError('Terrain pending/replacement GPU pixel contracts failed')
            if args.r2_contracts and not r2_contracts_passed(report,server.requests,directory):
                raise RuntimeError('Missing complete production R2 geometry/appearance/lifecycle evidence')
            if args.local_image_contracts and (not local_image_contracts_passed(report,directory) or not local_material_contracts_passed(report,directory) or not world_bump_contracts_passed(report,directory)):
                raise RuntimeError('Missing production local bitmap/material reload, immutable generation or GPU pixel evidence')
            if args.local_image_contracts and not remote_material_contracts_passed(report,server.requests,directory):
                raise RuntimeError('Missing production remote PBR transport, parse failure or account retirement evidence')
            if args.local_image_contracts and not shared_settings_contracts_passed(report,directory):
                raise RuntimeError('Missing actual shared preference effects and persisted values')
            if args.local_image_contracts and not r3_gltf_fixture.contracts_passed(report,directory):
                raise RuntimeError('Missing actual imported glTF image contracts')
            if args.local_image_contracts and not r3_terrain_contracts_passed(report,directory,server.requests):
                raise RuntimeError('Missing actual production terrain material, paint or settings evidence')
            if args.local_image_contracts and not media_contracts_passed(report,server.requests,directory):
                raise RuntimeError('Missing actual media plugin publication, shared ownership or GPU evidence')
            if args.local_image_contracts and not legacy_material_contracts_passed(report,server.requests,directory):
                raise RuntimeError('Missing actual legacy material capability, UV/factor or channel GPU evidence')
            if not environment_pixels_passed(report, directory):
                raise RuntimeError("Actual current environment UI edits did not reach preserved GPU pixels")
            if not parcel_rectangle_contracts_passed(report, directory):
                raise RuntimeError('Missing or incorrect actual parcel rectangle GPU readback')
            report['conference_start_request'] = conference_start_observed(server.requests)
            if not report['conference_start_request']:
                raise RuntimeError('Missing or incorrect actual authenticated conference start request')
            report['favorites_inventory_fetch'] = favorites_fetch_observed(server.requests)
            if not report['favorites_inventory_fetch']:
                raise RuntimeError('Native favorites never fetched their real account descendants')
            for suffix in ('', '-expected', '-expected-products', '-scene'):
                if (directory / ('session-chat' + suffix + '.ppm')).stat().st_size < 640 * 480 * 3:
                    raise RuntimeError('Missing native chat readback')
                if (directory / 'im' / ('session-chat' + suffix + '.ppm')).stat().st_size < 640 * 480 * 3:
                    raise RuntimeError('Missing native conversation UI readback')
            report['readback_sha256'] = {}
            for folder in (directory, directory / 'im'):
                with (folder / 'session-chat-scene.ppm').open('rb') as scene:
                    if scene.readline().strip() != b'P6':
                        raise RuntimeError('Invalid pre-UI scene image')
                    scene_width, scene_height = map(int, scene.readline().split())
                if (folder / 'session-chat-scene.rgba').stat().st_size != scene_width * scene_height * 4:
                    raise RuntimeError('Missing or truncated pre-UI RGBA input')
                for suffix in ('.ppm', '-expected.ppm', '-expected-products.ppm', '-scene.ppm', '-scene.rgba'):
                    image = folder / ('session-chat' + suffix)
                    report['readback_sha256'][str(image.relative_to(directory))] = hashlib.sha256(image.read_bytes()).hexdigest()
            report['readback_sha256']['parcel/rectangle.rgba'] = hashlib.sha256(
                (directory / 'parcel/rectangle.rgba').read_bytes()).hexdigest()
            if 'Machine Vulkan adapter: ' + harness['device']['name'] not in log:
                raise RuntimeError('Unexpected Vulkan adapter')
            libraries = loaded_library_hashes(log)
            for name, expected in harness['staged_library_sha256'].items():
                path = str(((stage if windows else stage / 'lib') / name).resolve())
                if libraries.get(path) != expected:
                    raise RuntimeError('Unexpected GHI dependency: ' + name)
            for library in runtime_libraries(runtime):
                if libraries.get(str(library.resolve())) != hashlib.sha256(library.read_bytes()).hexdigest():
                    raise RuntimeError('Viewer did not load the pinned ICD/validation library: ' + str(library))
            report['loaded_library_sha256'] = libraries
            report['ui_oracle_background'] = 'Independent pre-UI scene readback; CPU UI composition, full RGBA comparison. World rendering parity is qualified separately.'
            report['ui_blend_references'] = ['float-sum', 'destination-precision-products']
            report['ui_pixel_tolerance'] = 3
            profile_writes = [request.get('body', {}) for request in server.requests
                              if request.get('method') == 'PUT'
                              and request.get('path') == '/profile/'+server.account_id]
            report['profile_image_writes'] = {field: any(body.get(field) == '60000000-0000-0000-0000-000000000006'
                                                         for body in profile_writes)
                                             for field in ('sl_image_id', 'fl_image_id')}
            if not all(report['profile_image_writes'].values()):
                raise RuntimeError('Missing real self-profile image update requests')
            report['inventory_uploads'] = {}
            for kind, extension in (('texture', '.j2c'), ('sound', '.ogg')):
                receipts = [record for record in server.requests if record['path'] == '/upload-data/' + kind]
                body_path = server.upload_directory / (kind + extension)
                if len(receipts) != 1 or not body_path.is_file() or (
                        hashlib.sha256(body_path.read_bytes()).hexdigest() != receipts[0]['sha256']):
                    raise RuntimeError('Missing or inconsistent actual inventory upload bytes: ' + kind)
                report['inventory_uploads'][kind] = dict(bytes=receipts[0]['bytes'], sha256=receipts[0]['sha256'])
            script_receipts = [record for record in server.requests if record['path'] == '/script-data']
            script_path = server.upload_directory / 'script.lsl'
            if len(script_receipts) != 1 or not script_path.is_file() or (
                    hashlib.sha256(script_path.read_bytes()).hexdigest() != script_receipts[0]['sha256']):
                raise RuntimeError('Missing or inconsistent actual script upload bytes')
            report['inventory_uploads']['script'] = dict(bytes=script_receipts[0]['bytes'], sha256=script_receipts[0]['sha256'])

            passed = passed and server.auth_requests >= 3 and server.seed_requests >= 2 and server.event_requests >= 1
        except Exception as error:
            if isinstance(error, subprocess.TimeoutExpired):
                log = (error.stdout or b'').decode('utf-8', errors='replace') + '\nVIEWER TIMEOUT\n'
            log += '\nREPLAY EVIDENCE FAILURE: ' + str(error)
            report['failure'] = str(error)
            passed = False
        finally:
            server.replay_finished.set()
            server.shutdown(); worker.join()
        (directory / 'http-replay.json').write_text(json.dumps(server.requests, indent=2) + '\n')
    (directory / 'viewer.log').write_text(log, encoding='utf-8')
    report['viewer_executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
    report['source_qualified'] = False
    try:
        provenance = json.loads((build/'parity-build-provenance.json').read_text())
        root = Path(__file__).resolve().parents[1]
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain', '--', 'indra', 'scripts', 'tests'], cwd=root, text=True)
        if (not dirty.strip() and provenance.get('complete') is True and provenance.get('source_commit') == head and
                provenance.get('executable_sha256') == report['viewer_executable_sha256']):
            report['source_qualified'] = True
            report['source_commit'] = head
    except (OSError, ValueError, subprocess.CalledProcessError):
        pass  # Development replay evidence remains distinct from source qualification.
    report.update(passed=passed, exit_code=code, live_server_qualified=False,
                  presentation_qualified=False if args.headless else bool(harness.get('presentation_qualified')),
                  execution_budget=dict(viewer=VIEWER_REPLAY_SECONDS, process=PROCESS_REPLAY_SECONDS,
                                        matrix_case=CASE_REPLAY_SECONDS))
    (directory / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    report_case(case_label, started, passed, directory / 'viewer.log', log)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
