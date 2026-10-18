// Real message readers, session/scene owners and HTTP assets; no substitute scene.
// Diagnostic build only. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "llregionflags.h"
#include "vsr1replay.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "llframetimer.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llsdserialize.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llbitpack.h"
#include "llpacketring.h"
#include "message.h"
#include "net.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <cstdlib>
namespace
{
void require(bool condition,const char* text)
{
    if(!condition)
    { LL_WARNS("NativeR1Replay") << text << LL_ENDL;throw std::runtime_error(text); }
}
class Peer
{
    S32 socket=-1;LLPacketRing ring;
public:
    LLHost host;
    Peer() {int port=NET_USE_OS_ASSIGNED_PORT;require(start_net(socket,port)==0,"R1 peer socket creation failed");host=LLHost("127.0.0.1",port);}
    ~Peer() {if(socket>=0)end_net(socket);}
    void pump()
    {
        std::array<char,NET_BUFFER_SIZE> data;
        for(unsigned n=0;n<128;++n)
        {
            const S32 size=ring.receivePacket(socket,data.data());if(size<=0)break;
            // Echo actual reliable/template packets from the loopback viewer.
            // Its normal transport generates ACKs before message admission.
            const auto sender=ring.getLastSender();
            if(sender.getAddress()==host.getAddress())require(ring.sendPacket(socket,data.data(),size,sender),"R1 peer echo failed");
        }
    }
};
const LLUUID meshID("a1000000-0000-0000-0000-000000000001");
const LLUUID objectID("a2000000-0000-0000-0000-000000000001");
const LLUUID replacementID("a2000000-0000-0000-0000-000000000002");
const LLUUID childID("a2000000-0000-0000-0000-000000000004");
void objectPacket(const LLHost& host,U64 handle,U32 local,const LLUUID& id,U32 parent=0,bool mesh=false,
                  U32 meshFlags=0,U8 pcode=LL_PCODE_VOLUME,U8 attachment=0)
{
    LLMessageSystem* msg=gMessageSystem;msg->newMessage("ObjectUpdate");msg->nextBlock("RegionData");
    msg->addU64("RegionHandle",handle);msg->addU16("TimeDilation",65535);
    msg->nextBlock("ObjectData");msg->addU32("ID",local);msg->addU8("State",attachment);msg->addUUID("FullID",id);
    msg->addU32("CRC",0);msg->addU8("PCode",pcode);msg->addU8("Material",0);msg->addU8("ClickAction",0);
    msg->addVector3("Scale",LLVector3(1,1,1));
    const F32 pose[15]={15,20,30,1,0,0,.1f,0,0,0,0,0,0,0,.2f};
    std::array<F32,19> avatarPose{};std::copy(std::begin(pose),std::end(pose),avatarPose.begin()+4);
    if(pcode==LL_PCODE_LEGACY_AVATAR)msg->addBinaryData("ObjectData",avatarPose.data(),S32(sizeof(avatarPose)));
    else msg->addBinaryData("ObjectData",pose,sizeof(pose));
    msg->addU32("ParentID",parent);msg->addU32("UpdateFlags",0);
    LLVolumeParams params;LLVolumeMessage::packVolumeParams(&params,msg);
    LLPrimitive material;material.setNumTEs(6);for(U8 i=0;i<6;++i)material.setTEFullbright(i,1);material.packTEMessage(msg);
    for(const char* field:{"TextureAnim","NameValue","Data","Text","MediaURL","PSBlock"})msg->addBinaryData(field,nullptr,0);
    if(mesh || meshFlags)
    {
        std::vector<U8> extra((mesh?24:1)+(meshFlags?10:0));extra[0]=U8(unsigned(mesh)+unsigned(meshFlags!=0));
        if(mesh)
        {extra[1]=0x30;extra[3]=17;std::copy(meshID.mData,meshID.mData+UUID_BYTES,extra.begin()+7);extra[23]=LL_SCULPT_TYPE_MESH;}
        if(meshFlags)
        {
            const unsigned position=mesh?24:1;extra[position]=0x70;extra[position+2]=4;
            for(unsigned i=0;i<4;++i)extra[position+6+i]=U8(meshFlags>>(i*8));
        }
        msg->addBinaryData("ExtraParams",extra.data(),S32(extra.size()));
    }
    else msg->addBinaryData("ExtraParams",nullptr,0);
    const U8 color[]{255,255,255,255};msg->addBinaryData("TextColor",color,4);
    msg->addUUID("Sound",LLUUID::null);msg->addUUID("OwnerID",LLUUID::null);msg->addF32("Gain",0);msg->addU8("Flags",0);
    msg->addF32("Radius",0);msg->addU8("JointType",0);msg->addVector3("JointPivot",LLVector3::zero);msg->addVector3("JointAxisOrAnchor",LLVector3::zero);
    msg->sendReliable(host);
}
void meshFixture(const std::filesystem::path& path,unsigned revision)
{
    const std::array<U16,9> positions{0,0,32768,65535,0,32768,U16(revision==1?32768:45000),65535,32768};
    const std::array<U16,9> normals{32768,32768,65535,32768,32768,65535,32768,32768,65535};
    const std::array<U16,6> uv{0,0,65535,0,32768,65535};const std::array<U16,3> indices{0,1,2};
    auto binary=[](const auto& values){const auto* bytes=reinterpret_cast<const U8*>(values.data());return LLSD::Binary(bytes,bytes+sizeof(values));};
    LLSD face;face["Position"]=binary(positions);face["Normal"]=binary(normals);face["TexCoord0"]=binary(uv);face["TriangleList"]=binary(indices);
    face["PositionDomain"]["Min"]=ll_sd_from_vector3(LLVector3(-.5f,-.5f,-.5f));face["PositionDomain"]["Max"]=ll_sd_from_vector3(LLVector3(.5f,.5f,.5f));
    face["TexCoord0Domain"]["Min"]=LLSD::emptyArray();face["TexCoord0Domain"]["Min"].append(0);face["TexCoord0Domain"]["Min"].append(0);
    face["TexCoord0Domain"]["Max"]=LLSD::emptyArray();face["TexCoord0Domain"]["Max"].append(1);face["TexCoord0Domain"]["Max"].append(1);
    LLSD faces=LLSD::emptyArray();faces.append(face);const auto compressed=zip_llsd(faces);
    LLSD header;header["version"]=1;
    for(const char* lod:{"lowest_lod","low_lod","medium_lod","high_lod"}) {header[lod]["offset"]=0;header[lod]["size"]=S32(compressed.size());}
    std::ofstream out(path,std::ios::binary);LLSDSerialize::toBinary(header,out);out.write(compressed.data(),compressed.size());
    require(out.good(),"R1 mesh fixture write failed");
}
void terrainPacket(const LLHost& host)
{
    std::array<U8,64> data{};LLBitPack bits(data.data(),U32(data.size()));
    U16 stride=16,range=1;U8 size=16,type='M',quant=0x82,end=97,endCoefficients=2;F32 offset=24.5f;U32 id=(48u<<16)|20u;
    bits.bitPack(reinterpret_cast<U8*>(&stride),16);bits.bitPack(&size,8);bits.bitPack(&type,8);bits.bitPack(&quant,8);
    bits.bitPack(reinterpret_cast<U8*>(&offset),32);bits.bitPack(reinterpret_cast<U8*>(&range),16);bits.bitPack(reinterpret_cast<U8*>(&id),32);
    bits.bitPack(&endCoefficients,2);bits.bitPack(&end,8);const auto bytes=bits.flushBitPack();
    gMessageSystem->newMessage("LayerData");gMessageSystem->nextBlock("LayerID");gMessageSystem->addU8("Type",type);
    gMessageSystem->nextBlock("LayerData");gMessageSystem->addBinaryData("Data",data.data(),bytes);gMessageSystem->sendReliable(host);
}
LLSD ownerEvidence(const VSWorldScene& world,U32 local)
{
    const auto snapshot=world.evidence();
    for(const auto& row:llsd::inArray(snapshot["owners"]))
        if(ll_U64_from_sd(row["local"])==local)return row;
    return LLSD();
}
LLSD selfEvidence(const VSWorldScene& world)
{
    const auto snapshot=world.evidence();
    for(const auto& avatar:llsd::inArray(snapshot["avatars"]))if(avatar["self"].asBoolean())return avatar;
    return LLSD();
}
void taskReply(const LLHost& host,const LLUUID& id,S16 serial,const char* filename)
{
    gMessageSystem->newMessage("ReplyTaskInventory");gMessageSystem->nextBlock("InventoryData");
    gMessageSystem->addUUID("TaskID",id);gMessageSystem->addS16("Serial",serial);
    gMessageSystem->addString("Filename",filename);gMessageSystem->sendReliable(host);
}
void wearablesReply(VSNativeSession& owner)
{
    gMessageSystem->newMessage("AgentWearablesUpdate");gMessageSystem->nextBlock("AgentData");
    gMessageSystem->addUUID("AgentID",owner.account().agent);gMessageSystem->addUUID("SessionID",owner.account().session);
    gMessageSystem->addU32("SerialNum",1);gMessageSystem->nextBlock("WearableData");
    gMessageSystem->addUUID("ItemID",LLUUID("71000000-0000-0000-0000-000000000020"));
    gMessageSystem->addUUID("AssetID",meshID);gMessageSystem->addU8("WearableType",0);gMessageSystem->sendReliable(owner.host());
}
struct Replay
{
    std::unique_ptr<Peer> peer;LLHost original;U64 originalHandle=0;unsigned step=0,loggedStep=~0u;double deadline=0;
    unsigned motionStep=0;double motionTime=0;LLVector3 frozenPosition;LLQuaternion frozenRotation;
    VSNativeSession::AccountHandle account;
    VSWorldScene::ObjectHandle root,child,neighbour;VSWorldScene::FaceHandle face;
    std::function<bool()> callback,childCallback;std::optional<VSNativeSession::InventoryHandle> inventory;
    std::filesystem::path directory;LLSD evidence;bool disabledNeighbours=false;
    void pump(){if(peer)peer->pump();}
    bool tick(VSNativeSession& owner,LLSD& report)
    {
        if(loggedStep!=step)
        {loggedStep=step;LL_INFOS("NativeR1Replay") << "Ownership replay step " << step << LL_ENDL;}
        if(step==9){report["r1_contracts"]=evidence;return true;}
        require(owner.phase()==VSNativeSession::Phase::Connected,"R1 replay lost its connected account");
        auto& world=gViewerWindow->nativeContext()->world();
        const U64 peerHandle=(U64(512)<<32)|512;
        if(!step)
        {
            account=owner.account();original=owner.host();originalHandle=owner.handle();deadline=LLFrameTimer::getTotalSeconds()+120;
            evidence["self_before"]=selfEvidence(world);
            require(evidence["self_before"].isMap(),"R1 self-avatar startup owner is missing");
            disabledNeighbours=gSavedSettings.getBOOL("FSDisableNeighbourRegionConnections");gSavedSettings.setBOOL("FSDisableNeighbourRegionConnections",false);
            peer=std::make_unique<Peer>();directory=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"r1";
            std::filesystem::create_directories(directory);meshFixture(directory/"mesh-1.bin",1);meshFixture(directory/"mesh-2.bin",2);
            std::filesystem::remove(directory/"mesh-generation.txt");
            inventory=owner.inventory(LLUUID("71000000-0000-0000-0000-000000000020"));require(inventory.has_value(),"R1 inventory identity is missing");
            LLSD grant;grant["SimulatorInfo"][0]["Handle"]=ll_sd_from_U64(peerHandle);grant["SimulatorInfo"][0]["IP"]=ll_sd_from_ipaddr(peer->host.getAddress());
            grant["SimulatorInfo"][0]["Port"]=S32(peer->host.getPort());grant["SimulatorInfo"][0]["RegionSizeX"]=1024;grant["SimulatorInfo"][0]["RegionSizeY"]=512;
            require(owner.regionEvent("EnableSimulator",grant,original,owner.generation(),owner.circuitGeneration(original)),"R1 neighbor grant failed");
            objectPacket(original,originalHandle,950,objectID);objectPacket(original,originalHandle,951,childID,950);
            objectPacket(original,originalHandle,953,LLUUID("a2000000-0000-0000-0000-000000000005"),0,true,
                LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG);
            objectPacket(original,originalHandle,954,LLUUID("a2000000-0000-0000-0000-000000000006"),0,false,0,LL_PCODE_LEGACY_AVATAR);
            objectPacket(original,originalHandle,955,LLUUID("a2000000-0000-0000-0000-000000000007"),954,false,0,LL_PCODE_VOLUME,1);
            // R2 implements trees. Keep R1's no-substitute check on an actually
            // unknown wire PCode instead of requiring supported trees to fail.
            objectPacket(original,originalHandle,956,LLUUID("a2000000-0000-0000-0000-000000000008"),0,false,0,U8(0x7e));
            ++step;return false;
        }
        require(LLFrameTimer::getTotalSeconds()<deadline,"R1 ownership replay exceeded its deadline");
        if(step==1)
        {
            const auto a=world.object(original,950),b=world.object(original,951);if(!a||!b)return false;
            const auto control=world.object(original,953),avatar=world.object(original,954),attachment=world.object(original,955),unknown=world.object(original,956);
            if(!control || !avatar || !attachment || !unknown)return false;
            require(control->kind==VSWorldContracts::OwnerKind::ControlAvatar && avatar->kind==VSWorldContracts::OwnerKind::Avatar &&
                attachment->kind==VSWorldContracts::OwnerKind::Attachment && ownerEvidence(world,956)["geometry_pending"].asBoolean(),
                "R1 avatar/control/attachment identity or unsupported geometry policy failed");
            evidence["scene_owner_kinds"]=true;
            if(!motionStep)
            {
                root=*a;child=*b;const auto f=world.face(root,0);require(f.has_value(),"R1 face identity missing");face=*f;callback=owner.interactionGuard(face);
                const auto childFace=world.face(child,0);require(childFace.has_value(),"R1 linked face identity missing");
                childCallback=owner.interactionGuard(*childFace);require(childCallback(),"R1 linked face owner is not current");
                gMessageSystem->newMessage("AvatarAnimation");gMessageSystem->nextBlock("Sender");gMessageSystem->addUUID("ID",control->id);
                gMessageSystem->nextBlock("AnimationList");gMessageSystem->addUUID("AnimID",LLUUID("a3000000-0000-0000-0000-000000000001"));
                gMessageSystem->addS32("AnimSequenceID",17);gMessageSystem->sendReliable(original);
                require(callback() && owner.current(*inventory),"R1 current owner guards failed");
                require(world.freeze(child,true),"R1 selection freeze failed");
                const auto row=ownerEvidence(world,950);frozenPosition=ll_vector3_from_sd(row["predicted_position"]);
                frozenRotation=ll_quaternion_from_sd(row["predicted_rotation"]);motionTime=LLFrameTimer::getTotalSeconds();++motionStep;return false;
            }
            if(motionStep==1)
            {
                if(LLFrameTimer::getTotalSeconds()-motionTime<.15)return false;
                const auto row=ownerEvidence(world,950);
                require(ownerEvidence(world,953)["control_animations"].asInteger()==1,"R1 control-avatar animation owner was not updated");
                const auto snapshot=world.evidence();
                for(const auto& human:llsd::inArray(snapshot["avatars"]))require(human["id"].asUUID()!=control->id,"R1 control avatar created a human avatar substitute");
                require(row["selected"].asBoolean() && ll_vector3_from_sd(row["predicted_position"])==frozenPosition &&
                    ll_quaternion_from_sd(row["predicted_rotation"])==frozenRotation,"R1 selected linkset moved");
                require(world.freeze(child,false),"R1 selection thaw failed");motionTime=LLFrameTimer::getTotalSeconds();++motionStep;return false;
            }
            if(motionStep==2)
            {
                if(LLFrameTimer::getTotalSeconds()-motionTime<.15)return false;
                const auto row=ownerEvidence(world,950);
                require(ll_vector3_from_sd(row["authoritative_position"])==LLVector3(15,20,30) &&
                    ll_vector3_from_sd(row["predicted_position"])!=frozenPosition &&
                    ll_quaternion_from_sd(row["predicted_rotation"])!=frozenRotation,"R1 prediction changed authority or failed to thaw");
                evidence["motion_selection"]=row;
                require(owner.requestTaskInventory(root),"R1 task inventory request failed");taskReply(original,root.id,42,"r1-task.txt");wearablesReply(owner);
                gInventory.addChangedMask(LLInventoryObserver::LABEL,inventory->item);
                require(!owner.current(*inventory),"R1 inventory update retained its old revision");
                inventory=owner.inventory(inventory->item);require(inventory.has_value(),"R1 inventory replacement lost its identity");
                evidence["inventory_revision"]=true;++motionStep;return false;
            }
            const auto task=world.taskInventory(root);
            if(!task["known"].asBoolean() || !owner.evidence()["wearable_metadata_known"].asBoolean())return false;
            require(task["serial"].asInteger()==42 && task["filename"].asString()=="r1-task.txt" &&
                owner.evidence()["appearance_owner_initialized"].asBoolean(),"R1 task/appearance startup identity mismatch");
            evidence["task_inventory"]=task;evidence["appearance_startup"]=owner.evidence()["wearable_metadata"];
            LLSD handshake;handshake["name"]="R1 neighbor";handshake["id"]=LLUUID::generateNewID();handshake["access"]=13;handshake["flags64"]=ll_sd_from_U64(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE);
            require(owner.deliver("RegionHandshake",handshake,peer->host,owner.generation()),"R1 neighbor handshake failed");
            if(!owner.admit("ObjectUpdate",peer->host))return false;
            objectPacket(peer->host,peerHandle,950,objectID);terrainPacket(peer->host);objectPacket(original,originalHandle,950,replacementID);++step;return false;
        }
        if(step==2)
        {
            const auto replaced=world.object(original,950),other=world.object(peer->host,950);
            if(!replaced || replaced->id!=replacementID || !other)return false;
            require(!world.current(root) && !callback() && world.current(child),"R1 local-ID reuse retained an old interaction owner");
            require(!childCallback() && !world.freeze(child,true),"R1 reused parent retained a live linked interaction");
            require(other->id==objectID && other->host!=replaced->host,"R1 host-qualified IDs collided");neighbour=*other;
            const auto snapshot=world.evidence();if(snapshot["neighbours"].size()!=1 || snapshot["neighbours"][0]["terrain_patches"].asInteger()!=1)return false;
            evidence["host_ids"]=true;evidence["object_face_retirement"]=true;evidence["variable_terrain"]=true;evidence["initial_scene"]=snapshot;
            objectPacket(original,originalHandle,952,LLUUID("a2000000-0000-0000-0000-000000000003"),0,true);++step;return false;
        }
        if(step==3)
        {
            const auto snapshot=world.evidence();
            for(const auto& item:llsd::inArray(snapshot["owners"]))if(ll_U64_from_sd(item["local"])==952 && ll_U64_from_sd(item["asset_generation"])>0)
            {
                evidence["mesh_before"]=item;world.replaceAsset(meshID);std::ofstream(directory/"mesh-generation.txt")<<2;++step;return false;
            }
            return false;
        }
        if(step==4)
        {
            const auto snapshot=world.evidence();
            for(const auto& item:llsd::inArray(snapshot["owners"]))if(ll_U64_from_sd(item["local"])==952 &&
                ll_U64_from_sd(item["asset_generation"])>ll_U64_from_sd(evidence["mesh_before"]["asset_generation"]))
            {
                const auto before=ll_vector3_from_sd(evidence["mesh_before"]["asset_third_vertex"]);
                const auto after=ll_vector3_from_sd(item["asset_third_vertex"]);
                require((after-before).length()>.1f,"R1 asset generation changed without replacing decoded geometry");
                evidence["mesh_after"]=item;evidence["asset_replacement"]=true;++step;return false;
            }
            return false;
        }
        if(step==5)
        {
            LLSD crossing;crossing["agent"]=owner.account().agent;crossing["session"]=owner.account().session;
            crossing["ip"]=LLSD::Integer(peer->host.getAddress());crossing["port"]=S32(peer->host.getPort());crossing["handle"]=ll_sd_from_U64(peerHandle);
            crossing["seed"]=std::getenv("VS_VULKAN_REPLAY_SEED");crossing["width"]=1024;crossing["height"]=512;
            require(owner.deliver("CrossedRegion",crossing,original,owner.generation()),"R1 production crossing failed");
            require(world.current(neighbour) && world.evidence()["neighbours"].size()>0,"R1 crossing discarded the neighbor scene");
            evidence["self_crossed"]=selfEvidence(world);
            require(evidence["self_crossed"]["owner_generation"]==evidence["self_before"]["owner_generation"] &&
                ll_U64_from_sd(evidence["self_crossed"]["region_handle"])==peerHandle,"R1 crossing replaced the account self-avatar owner");
            evidence["crossing_continuity"]=true;++step;return false;
        }
        if(step==6)
        {
            LLSD handshake;handshake["name"]="R1 neighbor";handshake["id"]=LLUUID::generateNewID();handshake["access"]=13;handshake["flags64"]=ll_sd_from_U64(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE);
            owner.deliver("RegionHandshake",handshake,peer->host,owner.generation());
            LLSD movement;movement["agent"]=owner.account().agent;movement["session"]=owner.account().session;
            movement["handle"]=ll_sd_from_U64(peerHandle);movement["position"]=ll_sd_from_vector3(LLVector3(10,20,30));
            owner.deliver("AgentMovementComplete",movement,peer->host,owner.generation());
            if(!owner.regionReady())return false;
            LLSD back;back["agent"]=owner.account().agent;back["session"]=owner.account().session;back["ip"]=LLSD::Integer(original.getAddress());
            back["port"]=S32(original.getPort());back["handle"]=ll_sd_from_U64(originalHandle);back["seed"]=std::getenv("VS_VULKAN_REPLAY_SEED");
            require(owner.deliver("CrossedRegion",back,peer->host,owner.generation()),"R1 return crossing failed");++step;return false;
        }
        if(step==7)
        {
            LLSD handshake;handshake["name"]="Native replay region";handshake["id"]=LLUUID::generateNewID();handshake["access"]=13;handshake["flags64"]=ll_sd_from_U64(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE);
            owner.deliver("RegionHandshake",handshake,original,owner.generation());
            LLSD movement;movement["agent"]=owner.account().agent;movement["session"]=owner.account().session;
            movement["handle"]=ll_sd_from_U64(originalHandle);movement["position"]=ll_sd_from_vector3(LLVector3(10,20,30));
            owner.deliver("AgentMovementComplete",movement,original,owner.generation());if(!owner.regionReady())return false;
            LL_INFOS("NativeR1Replay") << "Return ready; child=" << world.current(child)
                << " neighbor=" << world.current(neighbour) << LL_ENDL;
            require(world.current(child) && world.current(neighbour),"R1 return crossing invalidated preserved owners");
            const auto self=selfEvidence(world);
            require(self["owner_generation"]==evidence["self_before"]["owner_generation"] &&
                ll_U64_from_sd(self["region_handle"])==originalHandle,"R1 return crossing failed to rebind the self-avatar owner");
            evidence["self_continuity"]=true;
            LLSD close=LLSD::emptyMap();
            require(owner.deliver("DisableSimulator",close,peer->host,owner.generation()),"R1 simulator retirement message rejected");
            require(!world.current(neighbour) && !owner.circuitGeneration(peer->host),"R1 retired region owner remained current");
            require(owner.current(*inventory),"R1 crossing changed account inventory identity");
            evidence["circuit_retirement"]=true;evidence["inventory_scope"]=true;++step;return false;
        }
        evidence["final_scene"]=world.evidence();report["r1_contracts"]=evidence;
        gSavedSettings.setBOOL("FSDisableNeighbourRegionConnections",disabledNeighbours);
        std::ofstream output(directory/"contracts.xml");LLSDSerialize::toPrettyXML(evidence,output);require(output.good(),"R1 evidence write failed");
        ++step;return true;
    }
    void retired(VSNativeSession& owner,LLSD& report)
    {
        auto& world=gViewerWindow->nativeContext()->world();
        require(!owner.current(account) && !owner.current(*inventory) && !world.current(child) && !callback(),
            "R1 logout retained a current account, inventory, object or interaction owner");
        const auto snapshot=world.evidence();
        require(snapshot["owners"].size()==0 && snapshot["neighbours"].size()==0 &&
            snapshot["assets"]["requests"].asInteger()==0 && ll_U64_from_sd(snapshot["assets"]["resident_bytes"])==0 &&
            snapshot["owned_object_slots"].asInteger()==0 && snapshot["owned_avatar_slots"].asInteger()==0,
            "R1 logout retained native scene owners or CPU payload reservations");
        report["r1_contracts"]["logout_retirement"]=snapshot;
    }
};
std::unique_ptr<Replay> replay;
}
void vs_r1_replay_pump(){if(replay)replay->pump();}
bool vs_r1_replay_tick(VSNativeSession& owner,LLSD& report)
{
    if(!std::getenv("VS_VULKAN_REPLAY_R1"))return true;
    if (!replay)
    {
        replay = std::make_unique<Replay>();
    }
    return replay->tick(owner, report);
}
void vs_r1_replay_cleanup(){replay.reset();}
void vs_r1_replay_retired(VSNativeSession& owner,LLSD& report){if(replay)replay->retired(owner,report);}
