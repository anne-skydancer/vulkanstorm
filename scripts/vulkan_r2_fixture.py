"""Loopback inventory and bake transport for production R2 replay."""
import copy
import json
from pathlib import Path
import threading
import struct
import hashlib
import vulkan_r2_geometry_fixture
from urllib.parse import urlsplit, parse_qs

COF='70000000-0000-0000-0000-000000000017'
APPEARANCE_FOLDERS = {COF, *(f'70000000-0000-0000-0000-{index:012d}' for index in (7, 8, 10))}
ANIMATION='c1000000-0000-0000-0000-000000000001'
MESH='c3000000-0000-0000-0000-000000000001'


def animation_bytes(*, locomotion=False):
    """Version 1.0 keyframe asset; locomotion includes the stand IK joint chain."""
    names = ['mShoulderRight']
    if locomotion:
        names += ['mPelvis', 'mHipLeft', 'mKneeLeft', 'mAnkleLeft',
                  'mHipRight', 'mKneeRight', 'mAnkleRight']
    header=struct.pack('<HHif',1,0,4,2.)+b'\0'
    header+=struct.pack('<ffiffII',0.,2.,1,.05,.05,0,len(names))
    joints = []
    for name in names:
        joint=name.encode()+b'\0'+struct.pack('<ii',4,2)
        joint+=struct.pack('<HHHH',0,32767,32767,32767)
        joint+=struct.pack('<HHHH',65535,32767,32767,49151 if name=='mShoulderRight' else 32767)
        joints.append(joint+struct.pack('<i',0))
    return header+b''.join(joints)+struct.pack('<i',0)


def configure(server,directory):
    server.r2_directory=directory
    server.r2_lock=threading.RLock()
    server.r2_state=None
    server.r2_uploads={}
    server.r2_next_asset=1
    server.r2_next_category=1
    server.r2_category_versions={}
    if directory is not None:
        Path(directory).mkdir(parents=True,exist_ok=True)
        (Path(directory)/(ANIMATION+'.anim')).write_bytes(animation_bytes())
        vulkan_r2_geometry_fixture.write(directory)


def state(server):
    if not getattr(server,'r2_directory',None):return None
    if server.r2_state is not None:return server.r2_state
    filename=Path(server.r2_directory)/'inventory.json'
    if not filename.is_file():return None
    manifest=json.loads(filename.read_text())
    items={item['item_id']:item for item in manifest['items']}
    links={}
    for index in range(1,5):
        source=items[f'b1000000-0000-0000-0000-{index:012d}']
        row=copy.deepcopy(source)
        row.update(item_id=f'b3000000-0000-0000-0000-{index:012d}',parent_id=COF,
                   asset_id=source['item_id'],linked_id=source['item_id'],type='link',desc='')
        links[row['item_id']]=row
    server.r2_state=dict(items=items,links=links,version=10,next_link=5,agent=manifest['agent'])
    return server.r2_state


def folder(server,identifier):
    if not getattr(server,'r2_directory',None):return None
    with server.r2_lock:
        current=state(server)
        if current is None:return None
        parents = {COF, *(row['parent_id'] for row in current['items'].values())}
        parents.update(row['folder_id'] for row in getattr(server,'r2_categories',[])
                       if row['folder_id'].startswith('b5000000-'))
        if identifier not in parents:return None
        return [copy.deepcopy(row) for row in [*current['items'].values(),*current['links'].values()]
                if row['parent_id']==identifier],(current['version'] if identifier==COF
                    else server.r2_category_versions.get(identifier,1))


def item(server,identifier):
    if not getattr(server,'r2_directory',None):return None
    with server.r2_lock:
        current=state(server)
        if current is None:return None
        row = current['items'].get(identifier) or current['links'].get(identifier)
        if row is None:
            row = getattr(server, 'r2_baseline_item', lambda _: None)(identifier)
        return copy.deepcopy(row)


def uploaded_item(server, identifier, asset, metadata):
    """Persist a completed ordinary upload for subsequent real AIS edits."""
    if not getattr(server, 'r2_directory', None):
        return
    with server.r2_lock:
        current = state(server)
        if current is None:
            raise ValueError('R2 inventory upload completed without an inventory owner')
        agent = current['agent']
        current['items'][identifier] = dict(item_id=identifier, asset_id=asset,
            parent_id=metadata['folder_id'], type=metadata['asset_type'], inv_type=metadata['inventory_type'],
            name=metadata['name'], desc=metadata['description'], flags=0, created_at=1,
            permissions=dict(creator_id=agent, owner_id=agent, last_owner_id=agent,
                             base_mask=2147483647, owner_mask=2147483647,
                             next_owner_mask=metadata['next_owner_mask'], group_mask=metadata['group_mask'],
                             everyone_mask=metadata['everyone_mask']), sale_info=dict(sale_type='not', sale_price=0))
        parent = metadata['folder_id']
        server.r2_category_versions[parent] = server.r2_category_versions.get(parent, 1) + 1


def get(handler,path,query):
    if not getattr(handler.server,'r2_directory',None):return False
    if path == '/r2-mesh' and query.get('mesh_id') == [MESH]:
        filename=Path(handler.server.r2_directory)/'rigged.mesh'
        if not filename.is_file():handler.send_error(404);return True
        data=filename.read_bytes()
        handler.server.requests.append(dict(path='r2-mesh',asset=MESH,bytes=len(data)))
        handler.send_response(200);handler.send_header('Content-Length',str(len(data)))
        handler.end_headers();handler.wfile.write(data);return True
    if path.startswith('/r2-ais/category/'):
        return get_category(handler,path,query)
    if path.startswith('/r2-ais/item/'):
        identifier=path.rsplit('/',1)[-1]
        row=item(handler.server,identifier)
        handler.server.requests.append(dict(path='r2-ais-item',item=identifier,found=row is not None))
        if row is None:handler.send_error(404)
        else:handler.respond_llsd(row)
        return True
    identifier=next((query[key][0] for key in ('bodypart_id','clothing_id','texture_id','animatn_id','animation_id','gesture_id') if query.get(key)),None)
    data=None
    with handler.server.r2_lock:
        if identifier in handler.server.r2_uploads:data=handler.server.r2_uploads[identifier]
        elif identifier:
            extension=('.legacy.j2c' if query.get('texture_id') else '.anim' if query.get('animatn_id') or query.get('animation_id') else
                       '.gesture' if query.get('gesture_id') else '.wearable')
            filename=Path(handler.server.r2_directory)/(identifier+extension)
            if filename.is_file():data=filename.read_bytes()
            elif extension == '.anim':
                catalog = Path(handler.server.r2_directory)/'motion-catalog.json'
                if catalog.is_file() and identifier in json.loads(catalog.read_text()).get('assets', []):
                    data = animation_bytes(locomotion=True)
    if data is None:return False
    handler.server.requests.append(dict(path='r2-asset',asset=identifier,bytes=len(data)))
    handler.send_response(200);handler.send_header('Content-Length',str(len(data)))
    handler.end_headers();handler.wfile.write(data);return True


def get_category(handler,path,query):
    server=handler.server
    identifier=path.split('/')[3]
    categories={row['folder_id']:row for row in getattr(server,'r2_categories',[])}
    aliases={'current':46,'root':8,'bodypart':13,'clothing':5,'favorite':23}
    if identifier in aliases:
        identifier=next((key for key,row in categories.items() if row['type_default']==aliases[identifier]),identifier)
    if identifier not in categories:handler.send_error(404);return True
    depth=min(50,max(0,int(query.get('depth',['0'])[0])))
    fetched=[]
    def category(key,remaining):
        source=categories[key]
        row=dict(source,category_id=key,agent_id='10000000-0000-0000-0000-000000000001')
        row.pop('folder_id')
        # The viewer publishes these contents only after connecting. Early root
        # fetches must not claim the unpublished folders are completely empty:
        # their delayed replies could overwrite a later populated fetch.
        if key in APPEARANCE_FOLDERS and state(server) is None:
            row.pop('version', None)
            fetched.append(dict(category=key, pending=True))
            return row
        contents=folder(server,key)
        rows,version=contents if contents is not None else (
            getattr(server,'r2_baseline_items',lambda _:[])(key),server.r2_category_versions.get(key,1))
        row['version']=version
        items={item['item_id']:item for item in rows if item['type']!='link'}
        links={item['item_id']:item for item in rows if item['type']=='link'}
        current=state(server)
        for link in links.values():
            if current is not None and link['linked_id'] in current['items']:
                link['_embedded']=dict(item=copy.deepcopy(current['items'][link['linked_id']]))
        children={child:category(child,remaining-1) if remaining else dict(child_row,category_id=child)
                  for child,child_row in categories.items() if child_row['parent_id']==key}
        row['_embedded']=dict(items=items,links=links,categories=children)
        fetched.append(dict(category=key,items=list(items),links=list(links)))
        return row
    with server.r2_lock:response=category(identifier,depth)
    server.requests.append(dict(path='r2-ais-category',categories=fetched))
    handler.respond_llsd(response);return True


def post(handler,path,request):
    if not getattr(handler.server,'r2_directory',None):return False
    if path=='/r2-bake':
        handler.respond_llsd(dict(state='upload',uploader=handler.server.url+'/r2-bake-upload'));return True
    if not path.startswith('/r2-ais/category/'):return False
    parent=path.rsplit('/',1)[-1]
    with handler.server.r2_lock:
        current=state(handler.server)
        if request.get('categories'):
            categories=getattr(handler.server,'r2_categories',[])
            if parent not in {row['folder_id'] for row in categories}:handler.send_error(400);return True
            created={}
            for source in request['categories']:
                identifier=f"b5000000-0000-0000-0000-{handler.server.r2_next_category:012d}"
                handler.server.r2_next_category+=1
                row=dict(source,category_id=identifier,parent_id=parent,agent_id='10000000-0000-0000-0000-000000000001',version=1,
                         _embedded=dict(items={},links={},categories={}))
                created[identifier]=row
                categories.append(dict(row,folder_id=identifier))
            version=handler.server.r2_category_versions.get(parent,1)+len(created)
            handler.server.r2_category_versions[parent]=version
            handler.respond_llsd(dict(_created_categories=list(created),_updated_category_versions={parent:version},
                                      _embedded=dict(categories=created)))
            return True
        if current is None or parent not in {row['folder_id'] for row in getattr(handler.server,'r2_categories',[])} | {COF}:
            handler.send_error(400);return True
        created={}
        for link in request.get('links',[]):
            source=current['items'].get(link.get('linked_id'))
            if source is None:handler.send_error(400);return True
            identifier=f"b3000000-0000-0000-0000-{current['next_link']:012d}"
            current['next_link']+=1
            row=copy.deepcopy(source)
            row.update(item_id=identifier,parent_id=parent,asset_id=source['item_id'],linked_id=source['item_id'],
                       type='link',desc=link.get('desc',''))
            current['links'][identifier]=row;created[identifier]=row
        if parent==COF:
            current['version']+=1;version=current['version']
        else:
            version=handler.server.r2_category_versions.get(parent,1)+1
            handler.server.r2_category_versions[parent]=version
        response=dict(_created_items=list(created),_updated_category_versions={parent:version},_embedded=dict(links=created))
    handler.respond_llsd(response);return True


def binary_post(handler,path,payload):
    if path!='/r2-bake-upload' or not getattr(handler.server,'r2_directory',None):return False
    if not payload or len(payload)>64*1024*1024:handler.send_error(400);return True
    with handler.server.r2_lock:
        identifier=f"b4000000-0000-0000-0000-{handler.server.r2_next_asset:012d}"
        handler.server.r2_next_asset+=1;handler.server.r2_uploads[identifier]=payload
        Path(handler.server.r2_directory).mkdir(parents=True,exist_ok=True)
        (Path(handler.server.r2_directory)/(identifier+'.j2c')).write_bytes(payload)
    handler.server.requests.append(dict(path='r2-bake-upload',asset=identifier,bytes=len(payload),
                                       sha256=hashlib.sha256(payload).hexdigest()))
    handler.respond_llsd(dict(state='complete',new_asset=identifier));return True


def modify(handler,method,path,request=None):
    if path.startswith('/r2-ais/category/') and getattr(handler.server,'r2_directory',None):
        identifier=path.rsplit('/',1)[-1]
        with handler.server.r2_lock:
            row=next((row for row in getattr(handler.server,'r2_categories',[]) if row['folder_id']==identifier),None)
            if row is None or method!='PATCH':handler.send_error(404);return True
            row.update({key:value for key,value in (request or {}).items() if key=='name'})
            parent=row['parent_id']
            version=handler.server.r2_category_versions.get(parent,1)+1
            handler.server.r2_category_versions[parent]=version
            response=dict(row,category_id=identifier,_updated_category_versions={parent:version})
            response.pop('folder_id')
        handler.server.requests.append(dict(path='r2-category-patch',category=identifier,request=request))
        handler.respond_llsd(response);return True
    if not path.startswith('/r2-ais/item/') or not getattr(handler.server,'r2_directory',None):return False
    identifier=path.rsplit('/',1)[-1]
    with handler.server.r2_lock:
        current=state(handler.server)
        if current is None:handler.send_error(404);return True
        collection = current['links'] if identifier in current['links'] else current['items']
        if identifier not in collection:handler.send_error(404);return True
        parent=collection[identifier]['parent_id']
        if method=='DELETE':
            del collection[identifier]
            response=dict(_removed_items=[identifier])
        else:
            row=collection[identifier];row.update({key:value for key,value in (request or {}).items() if key in ('desc','name')})
            response=copy.deepcopy(row)
        if parent==COF:
            current['version']+=1;version=current['version']
        else:
            version=handler.server.r2_category_versions.get(parent,1)+1
            handler.server.r2_category_versions[parent]=version
        response['_updated_category_versions']={parent:version}
    handler.server.requests.append(dict(path='r2-'+method.lower(),item=identifier,request=request))
    handler.respond_llsd(response);return True
