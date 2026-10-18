// Actual estate-ban scope/response remains identical to the reference model.
PORTS
WRAPPERS
struct LLAvatarActions{static bool canEstateKickOrTeleportHomeMultiple(uuid_vec_t&,bool){return true;}static void estateBanMultiple(const uuid_vec_t&);static bool callbackEstateBan(const LLSD&,const LLSD&);};struct GoldAvatarActions{static bool canEstateKickOrTeleportHomeMultiple(uuid_vec_t&,bool){return true;}static void estateBanMultiple(const uuid_vec_t&);static bool callbackEstateBan(const LLSD&,const LLSD&);};
GOLD
METHODS
int main(){for(bool own:{false,true})for(bool manager:{false,true})for(bool isGod:{false,true})for(int option=0;option<3;++option){estateOwner={own?7:9};estateManager=manager;god=isGod;regions.clear();notices.clear();GoldAvatarActions::estateBanMultiple({{7},{1},{2}});auto old=notices.back();notices.clear();LLAvatarActions::estateBanMultiple({{7},{1},{2}});auto note=notices.back();assert(note.name==old.name&&note.args.map.size()==old.args.map.size()&&note.payload["ids"].size()==3);for(const auto&arg:old.args.map)assert(note.args[arg.first].asString()==arg.second.asString());
LLSD notification;notification["payload"]=note.payload;deltas.clear();invoices=0;old.callback(notification,LLSD(option));const auto expected=deltas;const auto invoiceCount=invoices;deltas.clear();invoices=0;note.callback(notification,LLSD(option));assert(deltas==expected&&invoices==invoiceCount);for(const auto&delta:deltas)assert(delta.second!=estateOwner.value);
notices.clear();LLAvatarActions::estateBanMultiple({{1},{2}});auto retired=notices.back();++accountEpoch;deltas.clear();retired.callback(notification,LLSD(0));assert(deltas.empty());notices.clear();LLAvatarActions::estateBanMultiple({{1},{2}});retired=notices.back();++regionEpoch;retired.callback(notification,LLSD(0));assert(deltas.empty());
}}
