struct VSWorldScene{
 struct ObjectHandle{LLHost host;LLUUID id;U32 local;};
 struct Impl{
  struct Object{LLUUID id;U32 parent=0;bool live=true;};std::map<U32,Object>objects;VSObjectIndex<LLUUID>objectIndex;LLHost host=1;
  bool linksCurrent(const Object&value)const{++links[mode];return value.live;}
 };std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<LLHost,std::shared_ptr<VSWorldScene>>mNeighbours;
 std::optional<ObjectHandle>object(const LLUUID&)const;
 std::optional<ObjectHandle>object(LLHost host,U32 local)const{auto it=mImpl->objects.find(local);return it==mImpl->objects.end()?std::nullopt:std::optional<ObjectHandle>({host,it->second.id,local});}
};
