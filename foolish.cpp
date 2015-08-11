#include "define.h"
using namespace libtorrent;
//foolish插件
struct foolish_plugin : plugin
{
	//新加入
	boost::shared_ptr<torrent_plugin> new_torrent(torrent* torrentInfo, void* userdata){
		//TaskManage* taskManage = TaskManage::instance();
		//char* filename = (char*)userdata;
		
		//taskManage->getTaskByTaskId()


		return boost::shared_ptr<torrent_plugin>();
	}
	void added(aux::session_impl* session){
		//LOG_INFO("加载任务列表");
		//TaskManage::instance()->loadTasks();
	}
	virtual void on_alert(alert const* a){
		alert* b = const_cast<alert*>(a);
		DownloadSession::instance()->handleAlert(b);
	}
	virtual void on_tick(){
	//	LOG_INFO("on_tick");
	}
	virtual bool on_optimistic_unchoke(std::vector<policy::peer*>& /* peers */){
		//LOG_INFO("on_tick");
		return false;

	}
	virtual void save_state(entry&) const{
		//LOG_INFO("save");
	}
	virtual void load_state(lazy_entry const&){
		//LOG_INFO("load");
	}
};
