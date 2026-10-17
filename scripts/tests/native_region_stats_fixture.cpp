int main(){
 VSNativeSession owner;owner.mCircuits[1]={};
 auto apply=[&](LLHost host=1,U64 account=7,U64 circuit=9,U64 flags=0x100000040ULL,U32 capacity=22222){return owner.applyRegionStats(host,account,circuit,flags,capacity);};
 assert(!apply(2) && !apply(1,6) && !apply(1,7,8));assert(owner.mFlags==0 && !owner.mCircuits.at(1).objectCapacityKnown);
 assert(apply());auto prior=owner.regionSnapshots();assert(prior[0].objectCapacityKnown && prior[0].objectCapacity==22222 && prior[0].flags==0x100000040ULL);
 assert(!prior[0].estateManagerKnown && !prior[0].estateManager);
 owner.mCircuits.at(1).estateManagerKnown=true;owner.mCircuits.at(1).estateManager=true;assert(owner.regionSnapshots()[0].estateManager);
 assert(!prior[0].estateManagerKnown);assert(apply(1,7,9,5,0));assert(owner.mFlags==5 && owner.regionSnapshots()[0].objectCapacityKnown && owner.regionSnapshots()[0].objectCapacity==0);
 assert(prior[0].objectCapacity==22222 && prior[0].flags==0x100000040ULL);
 owner.mChangingRegion=true;assert(!apply());owner.mChangingRegion=false;
 owner.mCircuits.at(1).handshake=false;assert(!apply());owner.mCircuits.at(1).handshake=true;
 owner.mCircuits.at(1).acknowledged=false;assert(!apply());owner.mCircuits.at(1).acknowledged=true;
 owner.mPhase=VSNativeSession::Phase::Logout;assert(!apply());owner.mPhase=VSNativeSession::Phase::Connected;
 owner.mCircuits.clear();assert(!apply() && owner.regionSnapshots().empty());owner.mCircuits[1]={};owner.mShutdown=true;assert(!apply());
}
