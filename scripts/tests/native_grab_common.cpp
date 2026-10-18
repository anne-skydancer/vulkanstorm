struct Agent{int getID(){return 11;}int getSessionID(){return 12;}}gAgent;
struct Region{int getHost(){return 99;}};
struct LLViewerObject{bool valid=true;int regionCalls=0;U32 mLocalID=123;Region region;Region*getRegion(){++regionCalls;return valid?&region:nullptr;}};
struct VSObjectContext{U32 localID=123;LLHost host=99;std::function<bool()>current;};
struct LLMessageSystem{
 std::vector<std::string>events;
 void newMessageFast(const char*s){events.emplace_back(s);}void nextBlockFast(const char*s){events.emplace_back(s);}void nextBlock(const char*s){events.emplace_back(s);}
 void addUUIDFast(const char*s,int n){events.push_back(std::string(s)+":"+std::to_string(n));}void addU32Fast(const char*s,U32 n){events.push_back(std::string(s)+":"+std::to_string(n));}
 void addS32Fast(const char*s,S32 n){events.push_back(std::string(s)+":"+std::to_string(n));}
 void addVector3(const char*s,LLVector3 v){std::ostringstream out;out<<s<<":"<<v.x<<","<<v.y<<","<<v.z;events.push_back(out.str());}void addVector3Fast(const char*s,LLVector3 v){addVector3(s,v);}
 void sendMessage(int host){events.push_back("unreliable:"+std::to_string(host));}
}message,*gMessageSystem=&message;
