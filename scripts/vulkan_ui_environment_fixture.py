"""Loopback ExtEnvironment service: real shared request/reply bodies, no live account."""
import copy
import threading
import time
import uuid
import json
import xml.etree.ElementTree as ET

DEFAULT_DAY_ASSET='5646d39e-d3d7-6aff-ed71-30fc87d64a91'
ROUTE='/ui-environment'

def configure(server,read_llsd):
    server.environment_read_llsd=read_llsd
    server.environment_lock=threading.Lock()
    server.environment_states={}
    server.environment_revision=0

def decode(node):
    # Environment validation checks LLSD UUID types, including nested sky/water
    # texture IDs. The generic evidence reader intentionally flattens those.
    if node.tag=='llsd':return decode(list(node)[0])
    if node.tag=='map':
        children=list(node)
        if len(children)%2 or any(children[i].tag!='key' for i in range(0,len(children),2)):
            raise ValueError('Malformed environment map')
        return {children[i].text:decode(children[i+1]) for i in range(0,len(children),2)}
    if node.tag=='array':return [decode(child) for child in node]
    if node.tag=='uuid':return uuid.UUID(node.text or '00000000-0000-0000-0000-000000000000')
    if node.tag=='integer':return int(node.text or '0')
    if node.tag=='real':return float(node.text or '0')
    if node.tag=='boolean':return node.text in ('1','true')
    return node.text or ''

def default_day(server):
    path=server.environment_fixture
    deadline=time.monotonic()+60
    while not path.is_file() and time.monotonic()<deadline:time.sleep(.02)
    if not path.is_file():raise ValueError('Missing actual serialized shared default day-cycle fixture')
    data=decode(ET.fromstring(path.read_bytes()))
    if not isinstance(data,dict) or data.get('type')!='daycycle':
        raise ValueError('Invalid shared day-cycle fixture')
    # This is the simulator fixture's authoritative identity of the same real
    # asset it serves via ViewerAsset, never a mutation of the viewer's model.
    data['asset_id']=uuid.UUID(DEFAULT_DAY_ASSET)
    return data

def target(query):
    keys=set(query)
    if not keys.issubset({'parcelid','trackno'}):raise ValueError('Unexpected environment target')
    parcel=int(query.get('parcelid',['-1'])[0])
    if parcel not in (-1,31):raise ValueError('Unowned fixture parcel')
    if 'trackno' in query and int(query['trackno'][0]) not in range(5):raise ValueError('Invalid sky/water track')
    return parcel

def environment(server,parcel):
    value=dict(parcel_id=parcel,is_default=True,day_cycle=default_day(server),day_length=14400,
        day_offset=0,day_hash=1,day_names='Loopback day cycle',track_altitudes=[1000.,2000.,3000.],env_version=0)
    value.update(copy.deepcopy(server.environment_states.get(parcel,{})))
    return value

def get(handler,path,query):
    if path!=ROUTE:return False
    try:
        parcel=target(query)
        with handler.server.environment_lock:value=environment(handler.server,parcel)
    except (ValueError,OSError,ET.ParseError) as error:
        handler.respond_llsd({'success':False,'message':str(error)},400);return True
    handler.server.requests.append(dict(path=path,method='GET',query=query,parcel_id=parcel,env_version=value['env_version']))
    handler.respond_llsd({'success':True,'environment':value});return True

def modify(handler,method,path,query,request=None):
    if path!=ROUTE:return False
    try:
        parcel=target(query)
        if method not in ('PUT','DELETE'):raise ValueError('Unsupported environment method')
        if method=='PUT':
            if not isinstance(request,dict) or set(request)!={'environment'}:raise ValueError('Missing environment body')
            updates=request['environment']
            allowed={'day_cycle','day_asset','day_name','day_length','day_offset','track_altitudes','flags'}
            if not isinstance(updates,dict) or not updates or not set(updates).issubset(allowed):raise ValueError('Unexpected environment fields')
            if 'day_length' in updates and (type(updates['day_length']) is not int or updates['day_length']<=0):raise ValueError('Invalid day length')
            if 'day_cycle' in updates and (not isinstance(updates['day_cycle'],dict) or updates['day_cycle'].get('type')!='daycycle'):raise ValueError('Invalid serialized day cycle')
            if 'day_asset' in updates and str(updates['day_asset'])!=DEFAULT_DAY_ASSET:raise ValueError('Unknown fixture settings asset')
            if 'track_altitudes' in updates and (parcel!=-1 or len(updates['track_altitudes'])!=3):raise ValueError('Invalid region altitude targets')
        with handler.server.environment_lock:
            handler.server.environment_revision+=1
            if method=='DELETE':handler.server.environment_states.pop(parcel,None)
            else:
                state=handler.server.environment_states.setdefault(parcel,{})
                state.update(copy.deepcopy(updates));state['is_default']=False
                # A simulator may normalize the requested time interval. Return
                # an accepted value distinct from the optimistic 5.5h edit so
                # the real UI must consume this HTTP reply to show 6h.
                if 'day_length' in updates:
                    state['day_length']=((updates['day_length']+3599)//3600)*3600
                if 'day_asset' in updates:state['day_cycle']=default_day(handler.server)
            value=environment(handler.server,parcel)
            value['env_version']=handler.server.environment_revision
            handler.server.environment_states.setdefault(parcel,{})['env_version']=value['env_version']
    except (ValueError,TypeError,OSError,ET.ParseError) as error:
        handler.respond_llsd({'success':False,'message':str(error)},400);return True
    handler.server.requests.append(dict(path=path,method=method,query=query,parcel_id=parcel,
        env_version=value['env_version'],body=json.loads(json.dumps(request,default=str))))
    handler.respond_llsd({'success':True,'environment':value});return True

def exchange_observed(requests):
    updates=[row for row in requests if row.get('path')==ROUTE and row.get('method')=='PUT' and row.get('parcel_id')==31]
    return bool(updates) and any(row.get('body',{}).get('environment',{}).get('day_length',0)>0 for row in updates) and any(
        row.get('path')==ROUTE and row.get('method')=='GET' and row.get('parcel_id')==31 for row in requests)
