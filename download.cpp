#include "download.hpp"
#include "foolish.cpp"

using namespace libtorrent;

DownloadSession* DownloadSession::m_instance = 0;

DownloadSession::DownloadSession(){
	s = new libtorrent::session(fingerprint("LT", 1, 0, 0, 0));
	s->set_alert_mask(libtorrent::alert::all_categories
		& ~(libtorrent::alert::dht_notification
		+ libtorrent::alert::progress_notification
		+ libtorrent::alert::debug_notification
		+ libtorrent::alert::stats_notification));

	s->add_extension(&libtorrent::create_metadata_plugin);
	s->add_extension(&libtorrent::create_ut_metadata_plugin);
	s->add_extension(&libtorrent::create_ut_pex_plugin);
	s->add_extension(&libtorrent::create_smart_ban_plugin);

	boost::shared_ptr<plugin> foolish(new foolish_plugin());
	s->add_extension(foolish);
}
DownloadSession::~DownloadSession(){
	stopSession();
	delete s;
}
bool DownloadSession::setSessionDir(std::string temp, std::string down){
	tempDir = temp;
	downloadDir = down;

	std::vector<char> in;
	error_code ec;
	//目录处理
	if (!is_directory(tempDir, ec)){
		create_directory(tempDir, ec);
		if (ec)
			LOG_ERR("failed to create temp file directory: %s\n", ec.message().c_str());
	}
	//
	resumeDir = combine_path(tempDir, "resume");
	if (!is_directory(resumeDir, ec)){
		create_directory(resumeDir, ec);
		if (ec)
			LOG_ERR("failed to create resumeDir file directory: %s\n", ec.message().c_str());
	}
	torrentDir = combine_path(tempDir, "torrent");
	if (!is_directory(torrentDir, ec)){
		create_directory(torrentDir, ec);
		if (ec)
			LOG_ERR("failed to create resumeDir file directory: %s\n", ec.message().c_str());
	}

	//加载状态文件
	loadState();
	return true;
}
bool DownloadSession::startSession(int ListenPort, int UploadLimit, int DownloadLimit){
	bool result = false;
	error_code ec;
	try{
		session_settings settings;

		int listen_port = 6881;
		//ses.set_proxy(ps);
		if (ListenPort>0)
			listen_port = ListenPort;

		s->listen_on(std::make_pair(listen_port, listen_port + 10), ec);
		if (ec)
		{
			LOG_ERR("failed to listen on ports %d-%d: %s\n"
				, listen_port, listen_port + 1, ec.message().c_str());
			return result;
		}
		settings.use_dht_as_fallback = false;

		settings.user_agent = "Foolish/" LIBTORRENT_VERSION;
		settings.choking_algorithm = session_settings::auto_expand_choker;
		settings.disk_cache_algorithm = session_settings::avoid_readback;
		settings.volatile_read_cache = false;
		if (DownloadLimit > 0){
			settings.download_rate_limit = DownloadLimit * 1000;
		}
		else{
			settings.download_rate_limit = 0;
		}
		if (UploadLimit > 0){
			settings.upload_rate_limit = UploadLimit * 1000;
		}
		else{
			settings.upload_rate_limit = 0;
		}
		s->set_settings(settings);
		//速度设置

		s->start_lsd();
		s->start_upnp();
		s->start_natpmp();
		dht_settings dht;
		dht.privacy_lookups = true;
		s->set_dht_settings(dht);

		s->add_dht_router(std::make_pair(std::string("router.bittorrent.com"), 6881));
		s->add_dht_router(std::make_pair(std::string("router.utorrent.com"), 6881));
		s->add_dht_router(std::make_pair(std::string("dht.transmissionbt.com"), 6881));
		s->add_dht_router(std::make_pair(std::string("router.bitcomet.com"), 6881));
		s->add_dht_router(std::make_pair(std::string("dht.aelitis.com"), 6881)); // Vuze
		s->start_dht();
		loadTasks();
		result = true;
	}
	catch (std::exception& e) {
		LOG_ERR("failed to start session");

	}
	return result;
}

bool DownloadSession::stopSession(){
	bool result = false;
	try{
		s->pause();
		int num_outstanding_resume_data = 0;
		std::vector<libtorrent::torrent_handle> temp = s->get_torrents();
		for (std::vector<torrent_handle>::iterator i = temp.begin(); i != temp.end(); ++i)
		{
			torrent_handle& th = *i;
			torrent_status st = th.status(torrent_handle::query_accurate_download_counters);

			if (!th.is_valid())
			{
				LOG_INFO("  skipping, invalid handle\n");
				continue;
			}
			if (!st.has_metadata)
			{
				LOG_INFO("  skipping %s, no metadata\n", st.name.c_str());
				continue;
			}
			if (!st.need_save_resume)
			{
				LOG_INFO("  skipping %s, resume file up-to-date\n", st.name.c_str());
				continue;

			}
			th.save_resume_data();
			++num_outstanding_resume_data;
			LOG_INFO("\nwaiting for resume data [%d]\n", num_outstanding_resume_data);
		}
		while (num_outstanding_resume_data > 0)
		{
			alert const* a = s->wait_for_alert(seconds(10));
			if (a == 0) continue;

			std::deque<alert*> alerts;
			s->pop_alerts(&alerts);
			//std::string now = timestamp();
			for (std::deque<alert*>::iterator i = alerts.begin()
				, end(alerts.end()); i != end; ++i)
			{
				// make sure to delete each alert
				std::auto_ptr<alert> a(*i);

				torrent_paused_alert const* tp = alert_cast<torrent_paused_alert>(*i);
				if (tp)
				{

					continue;
				}

				if (alert_cast<save_resume_data_failed_alert>(*i))
				{

					--num_outstanding_resume_data;

					continue;
				}

				save_resume_data_alert const* rd = alert_cast<save_resume_data_alert>(*i);
				if (!rd) continue;
				--num_outstanding_resume_data;

				if (!rd->resume_data) continue;

				torrent_handle h = rd->handle;
				//torrent_status st2 = h.status(torrent_handle::query_save_path);
				std::vector<char> out;
				bencode(std::back_inserter(out), *rd->resume_data);
				std::string filename = libtorrent::combine_path(tempDir, libtorrent::combine_path("resume", libtorrent::to_hex(h.info_hash().to_string()) + ".resume"));
				saveFile(filename, out);
			}
		}

		saveState();
		s->abort();
		result = true;

	}
	catch (std::exception& e) {
		LOG_ERR("failed to start session");

	}
	return result;
}
std::string DownloadSession::getSessionStatusJson(){
	std::string out;
	char str[500]; memset(str, 0, 500);
	libtorrent::session_status s_s = s->status();

	snprintf(str, sizeof(str),
		"{"
		"\"total_download\":\"%f\","
		"\"total_up\":\"%f\","
		"\"num_peers\":\"%d\"}"
		, s_s.total_download
		, s_s.total_upload
		, s_s.total_upload
		, s_s.num_peers
		);
	out += str;
	return out;
}

std::string DownloadSession::getSessionDetailJson(){

	s->post_torrent_updates();
	std::vector<libtorrent::torrent_handle> torrents = s->get_torrents();
	std::vector<libtorrent::torrent_handle>::const_iterator it = torrents.begin();
	std::vector<libtorrent::torrent_handle>::const_iterator itend = torrents.end();

	char const* state_str[] =
	{ "checking (q)", "checking", "dl metadata", "downloading", "finished", "seeding", "allocating", "checking (r)" };

	char str[500];
	std::string comma = "";
	std::string out;

	for (; it != itend; ++it) {
		memset(str, 0, 500);

		libtorrent::torrent_status status = it->status(0x0);

		//it->status(0xffffffff);
		//status.progress_ppm
		boost::intrusive_ptr<libtorrent::torrent_info const> ti = it->torrent_file();
		libtorrent::size_type file_size = 0;
		/*
		for (int i = 0; i < ti->num_files(); ++i)
		{
		file_size += ti->files().file_size(i);
		}*/
		snprintf(str, sizeof(str),
			"{"
			"\"file\":\"%s\","
			"\"state\":\"%s\","
			"\"hash\":\"%s\","
			"\"progress\":\"%f\","
			"\"download_rate\":\"%d\","
			"\"upload_rate\":\"%d\","
			"\"total_download\":\"%d\","
			"\"total_upload\":\"%d\","
			"\"num_peers\":\"%d\","
			"\"total_size\":\"%d\""
			"}"
			, it->name().c_str()
			, state_str[status.state]
			, libtorrent::to_hex(it->info_hash().to_string()).c_str()
			, status.progress
			, status.download_payload_rate
			, status.upload_payload_rate
			, status.total_payload_upload
			, status.total_payload_upload
			, status.num_peers
			, file_size
			);

		//LOG_INFO("%s", status.pieces.bytes());
		out += (comma + str);
		//out = comma+out;
		comma = ",";
	}
	out = "[" + out + "]";
	return out;
}
bool  DownloadSession::addTorrentFromUrl(std::string &url){
	bool result = false;
	std::string TorrentFile = "";

	std::vector<add_torrent_params> magnet_links;
	std::vector<std::string> torrents;

	bool seed_mode = false;
	bool share_mode = false;
	bool disable_storage = false;

	try{
		//
		int taskStatus = getTaskStatus(url);

		if (taskStatus >0){ //如果存在
			//reStart(url);
			return true;
		}

		boost::intrusive_ptr<libtorrent::torrent_info> t;
		libtorrent::error_code ec;
		libtorrent::torrent_handle th;
		add_torrent_params p;
		const char* urlc = url.c_str();

		if (std::strstr(urlc, "http://") == urlc
			|| std::strstr(urlc, "https://") == urlc
			|| std::strstr(urlc, "magnet:") == urlc)
		{
			add_torrent_params p;
			if (seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
			if (disable_storage) p.storage = disabled_storage_constructor;
			if (share_mode) p.flags |= add_torrent_params::flag_share_mode;
			p.save_path = downloadDir;
			libtorrent::storage_mode_t storageMode = libtorrent::storage_mode_sparse;
			switch (StorageMode){
			case 0: storageMode = libtorrent::storage_mode_allocate; break;
			case 1: storageMode = libtorrent::storage_mode_sparse; break;
			case 2: storageMode = libtorrent::storage_mode_compact; break;
			}
			p.storage_mode = storageMode;
			p.url = url;
			std::vector<char> buf;

			//if magnet load resume
			if (std::strstr(urlc, "magnet:") == urlc)
			{
				add_torrent_params tmp;
				ec.clear();
				parse_magnet_uri(url, tmp, ec);

				if (ec) {
					std::string errorMessage = ec.message();
					LOG_ERR("Couldn't parse this Magnet URI: %s %s\n", urlc, errorMessage.c_str());
					return "";
				}
				std::string filename = libtorrent::combine_path(tempDir, libtorrent::combine_path("resume", libtorrent::to_hex(tmp.info_hash.to_string()) + ".resume"));
				LOG_INFO("load resume_data:%s", filename.c_str());
				loadFile(filename.c_str(), p.resume_data, ec);

				if (ec){
					LOG_ERR("fail to load resume_data%s\n", ec.message().c_str());
				}

				result = true;
			}

			LOG_INFO("adding URL: %s\n", urlc);

			p.userdata = (void*)strdup(urlc);
			s->async_add_torrent(p);

			//setTaskFromHandle(url, add_to_queued, th);
		}
	}
	catch (...) {
		LOG_ERR("Exception: failed to add torrent");

	}
	return result;
}
bool DownloadSession::addTorrentFromFile(std::string &torrentFile){
	bool result = false;

	try{

		int taskStatus = getTaskStatus(torrentFile);

		if (taskStatus >0){ //如果存在
			//reStart(url);
			return true;
		}

		boost::intrusive_ptr<libtorrent::torrent_info> t;
		libtorrent::error_code ec;
		libtorrent::torrent_handle th;
		add_torrent_params p;

		t = new libtorrent::torrent_info(torrentFile, ec);
		if (ec){
			std::string errorMessage = ec.message();
			LOG_ERR("%s: %s\n", torrentFile.c_str(), errorMessage.c_str());
		}
		th = s->find_torrent(t->info_hash());
		if (th.is_valid()) {
			LOG_INFO("Torrent is already in download list");
			result = true;
		}
		else{
			LOG_INFO("%s\n", t->name().c_str());
			LOG_INFO("StorageMode: %d\n", StorageMode);

			libtorrent::add_torrent_params torrentParams;
			libtorrent::lazy_entry resume_data;

			std::string filename = libtorrent::combine_path(tempDir, libtorrent::combine_path("resume", libtorrent::to_hex(t->info_hash().to_string()) + ".resume"));
			LOG_INFO("load resume_data:%s", filename.c_str());

			loadFile(filename.c_str(), torrentParams.resume_data, ec);

			if (ec){
				LOG_ERR("fail to load resume_data%s\n", ec.message().c_str());
			}

			torrentParams.ti = t;
			torrentParams.save_path = downloadDir;
			torrentParams.duplicate_is_error = false;
			torrentParams.auto_managed = true;
			libtorrent::storage_mode_t storageMode = libtorrent::storage_mode_sparse;
			switch (StorageMode){
			case 0: storageMode = libtorrent::storage_mode_allocate; break;
			case 1: storageMode = libtorrent::storage_mode_sparse; break;
			case 2: storageMode = libtorrent::storage_mode_compact; break;
			}
			torrentParams.storage_mode = storageMode;
			torrentParams.userdata = (void*)strdup(torrentFile.c_str());
			s->async_add_torrent(torrentParams);
			//copyfile
			std::vector<char> in;
			error_code ec;
			if (loadFile(torrentFile, in, ec) == 0)
			{
				std::string bakfile = libtorrent::combine_path(tempDir, libtorrent::combine_path("torrent", libtorrent::to_hex(t->info_hash().to_string()) + ".torrent"));
				saveFile(bakfile, in);
			}
			//setTaskFromHash(torrentFile, add_to_queued, libtorrent::to_hex(t->info_hash().to_string()));
			result = true;
		}

	}
	catch (...) {
		LOG_ERR("Exception: failed to add torrent");
	}
	return result;

}
void DownloadSession::readAlerts(){

	typedef std::vector<alert*> alerts_t;

	//m_alertDispatcher->getPendingAlertsNoWait(alerts);
	std::deque<alert*> alerts;
	s->pop_alerts(&alerts);
	for (std::deque<alert*>::iterator i = alerts.begin()
		, end(alerts.end()); i != end; ++i)
	{
		handleAlert(*i);
		delete *i;
	}
}

void DownloadSession::handleAlert(libtorrent::alert* a){

	try {
		switch (a->type()) {
		case torrent_finished_alert::alert_type:
			handleTorrentFinishedAlert(static_cast<torrent_finished_alert*>(a));
			break;
		case save_resume_data_alert::alert_type:
			handleSaveResumeDataAlert(static_cast<save_resume_data_alert*>(a));
			break;
		case add_torrent_alert::alert_type:
			handleAddTorrentAlert(static_cast<add_torrent_alert*>(a));
			break;
		case torrent_deleted_alert::alert_type:
			//handleTorrentDeletedAlert(static_cast<torrent_deleted_alert*>(a));
			break;
		case metadata_received_alert::alert_type:
			handleMetadataReceivedAlert(static_cast<metadata_received_alert*>(a));
			break;
		case file_error_alert::alert_type:
			//handleFileErrorAlert(static_cast<file_error_alert*>(a));
			break;
		case file_completed_alert::alert_type:
			//handleFileCompletedAlert(static_cast<file_completed_alert*>(a));
			break;
		case torrent_paused_alert::alert_type:
			handleTorrentPausedAlert(static_cast<torrent_paused_alert*>(a));
			break;
		case state_update_alert::alert_type:
			handleStateUpdateAlert(static_cast<state_update_alert*>(a));
			break;
		case portmap_error_alert::alert_type:
			//handlePortmapWarningAlert(static_cast<portmap_error_alert*>(a));
			break;
		case portmap_alert::alert_type:
			//handlePortmapAlert(static_cast<portmap_alert*>(a));
			break;
		default:
			std::string event_string;
			print_alert(a, event_string);

		}

	}
	catch (const std::exception& e) {
	}
}
void DownloadSession::print_alert(libtorrent::alert const* a, std::string& str)
{
	using namespace libtorrent;

#ifdef ANSI_TERMINAL_COLORS
	if (a->category() & alert::error_notification)
	{
		str += esc("31");
	}
	else if (a->category() & (alert::peer_notification | alert::storage_notification))
	{
		str += esc("33");
	}
#endif
	str += "[";
	str += std::time(0);
	str += "] ";
	str += a->message();
#ifdef ANSI_TERMINAL_COLORS
	str += esc("0");
#endif
	LOG_INFO(str.c_str());

	//if (g_log_file)
	//fprintf(g_log_file, "[%s] %s\n", timestamp(), a->message().c_str());
}
DownloadSession* DownloadSession::instance(){
	if (!m_instance) {
		m_instance = new DownloadSession;
	}
	return m_instance;
}

int DownloadSession::loadFile(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::get_generic_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		return -1;
	}
	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::get_generic_category());
		fclose(f);
		return -1;
	}

	fclose(f);
	if (r != s) return -3;
	return 0;
}

int DownloadSession::saveFile(std::string const& filename, std::vector<char>& v)
{
	FILE* f = fopen(filename.c_str(), "wb");
	if (f == NULL)
		return -1;

	int w = fwrite(&v[0], 1, v.size(), f);
	if (w < 0)
	{
		fclose(f);
		return -1;
	}

	if (w != int(v.size())) return -3;
	fclose(f);
	return 0;
}

void DownloadSession::loadState(){
	std::string filename = libtorrent::combine_path(tempDir, "session.state");
	std::vector<char> in;
	error_code ec;
	if (loadFile(filename, in, ec) == 0)
	{
		lazy_entry e;
		if (lazy_bdecode(&in[0], &in[0] + in.size(), e, ec) == 0)
			s->load_state(e);
	}
}

void DownloadSession::saveState()
{
	entry session_state;
	s->save_state(session_state);

	std::vector<char> out;
	bencode(std::back_inserter(out), session_state);
	std::string filename = libtorrent::combine_path(tempDir, "session.state");
	saveFile(filename, out);
}


void DownloadSession::handleAddTorrentAlert(libtorrent::add_torrent_alert* p){
	std::string filename;
	if (p->params.userdata){
		filename = (char*)p->params.userdata;
		free(p->params.userdata);
	}
	if (p->error){
		LOG_ERR("failed to add torrent: %s %s\n", filename.c_str(), p->error.message().c_str());
	}
	else{
		torrent_handle h = p->handle;

		if (!filename.empty()){
			//新增
			addTask(filename, add_to_queued, h);
			//setTaskFromHandle(filename, add_to_queued, h);
		}
	}
}
void DownloadSession::handleMetadataReceivedAlert(libtorrent::metadata_received_alert* p) {
	torrent_handle h = p->handle;
	boost::intrusive_ptr<torrent_info const> ti;
	if (h.is_valid()) {
		if (!ti) ti = h.torrent_file();
		create_torrent ct(*ti);
		entry te = ct.generate();
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), te);
		std::string filename = libtorrent::combine_path(tempDir, libtorrent::combine_path("torrent", libtorrent::to_hex(h.info_hash().to_string()) + ".torrent"));

		saveFile(filename, buffer);

		TaskInfo* taskinfo = getTaskByHandle(h);
		std::string taskId = taskinfo->taskId;
		LOG_INFO("handleMetadataReceivedAlert:%s", taskId.c_str());
		LOG_INFO("infohash:%s", taskId.c_str());

		if (taskId != ""){
			setTask(taskId, add_to_queued, libtorrent::to_hex(h.info_hash().to_string()));
		}

	}
	
}

void DownloadSession::handleSaveResumeDataAlert(libtorrent::save_resume_data_alert* p){
	torrent_handle h = p->handle;
	if (p->resume_data)
	{
		std::vector<char> out;
		bencode(std::back_inserter(out), *p->resume_data);
		torrent_status st = h.status(torrent_handle::query_save_path);
		std::string filename = libtorrent::combine_path(tempDir, libtorrent::combine_path("resume", libtorrent::to_hex(h.info_hash().to_string()) + ".resume"));
		saveFile(filename, out);
	}
}
void DownloadSession::handleTorrentPausedAlert(libtorrent::torrent_paused_alert* p){
	torrent_handle h = p->handle;
	if (h.is_valid()) {
		h.save_resume_data();
	}
}
void DownloadSession::handleTorrentFinishedAlert(libtorrent::torrent_finished_alert* p){
	torrent_handle h = p->handle;
	if (h.is_valid()) {
		h.save_resume_data();
		TaskInfo* taskinfo = getTaskByHandle(h);
		std::string taskId = taskinfo->taskId;
		LOG_INFO("download finish: %s", taskId.c_str());
		if (taskId != ""){
			setTask(taskId, finish_all, std::string(""));
		}
	}
}

void DownloadSession::handleStateUpdateAlert(libtorrent::state_update_alert *p) {
	std::vector<torrent_status> torrent_status = p->status;
	/*
	for (int i = 0; i < torrent_status.size(); i++) {
	TaskInfo* taskinfo = getTaskByHandle(torrent_status[i].handle);
	std::string taskId = taskinfo->taskId;
	if (torrent_status[i].handle.is_valid()){
	tasks.insert(std::pair<std::string, TaskInfo*>(taskId, taskinfo));
	}
	}*/
}
void DownloadSession::addTask(std::string& taskId, int status, libtorrent::torrent_handle& handle){

		TaskInfo* taskInfo = new TaskInfo();
		taskInfo->taskId = taskId;
		taskInfo->status = status;
		taskInfo->handle = handle;
		if (handle.is_valid()){
			taskInfo->hash = libtorrent::to_hex(handle.info_hash().to_string());
		}
		tasks.insert(std::pair<std::string, TaskInfo*>(taskId, taskInfo));
		saveTasks();
}

void DownloadSession::setTask(std::string& taskId, int status, std::string& hash){
	std::map<std::string, TaskInfo*> ::iterator iter;
	iter = tasks.find(taskId);
	if (iter != tasks.end()){
		if (hash != ""){
			iter->second->hash = hash;
		}
		iter->second->status = status;
		saveTasks();
	}
}

torrent_handle DownloadSession::getTorrentHandleFromHash(std::string &hash) const{
	libtorrent::sha1_hash ih;
	libtorrent::from_hex(hash.c_str(), 40, (char*)&ih[0]);
	return s->find_torrent(ih);
}

torrent_handle DownloadSession::getTorrentHandleFromTaskId(std::string &taskId){

	libtorrent::torrent_handle handle;
	std::map<std::string, TaskInfo*> ::iterator iter;
	iter = tasks.find(taskId);
	if (iter != tasks.end()){
		TaskInfo* taskinfo = iter->second;
		handle = getTorrentHandleFromHash(taskinfo->hash);
		if (!handle.is_valid() && taskinfo->handle.is_valid()){
			handle = taskinfo->handle;
		}
	}
	return handle;

}
int DownloadSession::getTaskStatus(std::string &taskId){
	int result = -1;
	s->post_torrent_updates();
	std::map<std::string, TaskInfo*> ::iterator iter;
	iter = tasks.find(taskId);
	if (iter != tasks.end()){
		TaskInfo* taskinfo = iter->second;
		result = taskinfo->status;
		if (result == add_to_queued){
			libtorrent::torrent_handle handle = getTorrentHandleFromHash(taskinfo->hash);
			if (handle.is_valid()){
				result = handle.status(0x0).state;
			}
		}
	}
	return result;
}

void DownloadSession::saveTasks(){
	std::map<std::string, TaskInfo*> ::iterator iter;
	entry::dictionary_type ret;
	for (iter = tasks.begin(); iter != tasks.end(); ++iter){
		entry::dictionary_type fileInfo;
		TaskInfo* task;
		task = iter->second;
		fileInfo["taskId"] = task->taskId;
		fileInfo["status"] = task->status;
		fileInfo["hash"] = task->hash;
		ret[iter->first] = fileInfo;
	}
	std::vector<char> out;
	bencode(std::back_inserter(out), ret);
	std::string filename = libtorrent::combine_path(tempDir, "task.list");
	saveFile(filename, out);
}

void DownloadSession::loadTasks(){

	std::string filename = libtorrent::combine_path(tempDir, "task.list");
	std::vector<char> in;
	error_code ec;
	if (loadFile(filename, in, ec) == 0)
	{
		lazy_entry e;
		if (lazy_bdecode(&in[0], &in[0] + in.size(), e, ec) == 0){
			for (int i = 0; i < e.dict_size(); i++){
				std::pair<std::string, lazy_entry const*> elem = e.dict_at(i);
				//elem.second->dict_find_string_value("hash");
				int status = elem.second->dict_find_int_value("status");
				TaskInfo* taskInfo = new TaskInfo();
				taskInfo->taskId = elem.second->dict_find_string_value("taskId");
				taskInfo->status = elem.second->dict_find_int_value("status");
				taskInfo->hash = elem.second->dict_find_string_value("hash");
				libtorrent::torrent_handle handle;
				if (taskInfo->hash != "00000000000000000000000000000000000000000000" && taskInfo->hash!=""){
					if (taskInfo->status == add_to_queued || taskInfo->status == download_suspend){

						std::string torrentFile = libtorrent::combine_path(tempDir, libtorrent::combine_path("torrent", taskInfo->hash + ".torrent"));
						if (libtorrent::exists(torrentFile)){
							handle = addTorrentFromTask(torrentFile);
							if (handle.is_valid()){
								taskInfo->handle = handle;
								tasks.insert(std::pair<std::string, TaskInfo*>(taskInfo->taskId, taskInfo));
							}
						}
					}
					else{
						tasks.insert(std::pair<std::string, TaskInfo*>(taskInfo->taskId, taskInfo));
					}

				}

				//}
			}

		}

		s->load_state(e);
	}

}
TaskInfo* DownloadSession::getTaskByHandle(libtorrent::torrent_handle& handle){
	std::map<std::string, TaskInfo*> ::iterator iter;
	TaskInfo* task = new TaskInfo();
	for (iter = tasks.begin(); iter != tasks.end(); ++iter){
		if (iter->second->handle == handle){
			return iter->second;
		}
	}
	return task;
}

libtorrent::torrent_handle DownloadSession::addTorrentFromTask(std::string torrentFile){
	libtorrent::torrent_handle th;
	try{

		boost::intrusive_ptr<libtorrent::torrent_info> t;
		libtorrent::error_code ec;
		add_torrent_params p;

		t = new libtorrent::torrent_info(torrentFile, ec);
		if (ec){
			std::string errorMessage = ec.message();
			LOG_ERR("%s: %s\n", torrentFile.c_str(), errorMessage.c_str());
		}
		th = s->find_torrent(t->info_hash());
		if (th.is_valid()) {
			LOG_INFO("Torrent is already in download list");

		}
		else{
			LOG_INFO("%s\n", t->name().c_str());
			LOG_INFO("StorageMode: %d\n", StorageMode);

			libtorrent::add_torrent_params torrentParams;
			libtorrent::lazy_entry resume_data;

			std::string filename = libtorrent::combine_path(tempDir, libtorrent::combine_path("resume", libtorrent::to_hex(t->info_hash().to_string()) + ".resume"));
			LOG_INFO("load resume_data:%s", filename.c_str());

			loadFile(filename.c_str(), torrentParams.resume_data, ec);

			if (ec){
				LOG_ERR("fail to load resume_data%s\n", ec.message().c_str());
			}

			torrentParams.ti = t;
			torrentParams.save_path = downloadDir;
			torrentParams.duplicate_is_error = false;
			torrentParams.auto_managed = true;
			libtorrent::storage_mode_t storageMode = libtorrent::storage_mode_sparse;
			switch (StorageMode){
			case 0: storageMode = libtorrent::storage_mode_allocate; break;
			case 1: storageMode = libtorrent::storage_mode_sparse; break;
			case 2: storageMode = libtorrent::storage_mode_compact; break;
			}
			torrentParams.storage_mode = storageMode;
			//torrentParams.userdata = (void*)strdup(torrentFile.c_str());
			th = s->add_torrent(torrentParams);
		}
	}
	catch (...) {
		LOG_ERR("Exception: failed to add torrent");
	}
	return th;
}

//获取文件信息
std::string DownloadSession::getTorrentFiles(std::string& taskId){
	std::string result = "";
	try {
		libtorrent::torrent_handle pTorrent = getTorrentHandleFromTaskId(taskId);

		if (pTorrent.is_valid()){
			std::string out;
			libtorrent::torrent_status s = pTorrent.status();
			if (pTorrent.status().has_metadata) {
				libtorrent::torrent_info const& info = pTorrent.get_torrent_info();
				int files_num = info.num_files();
				std::string comma = "";
				char str[500];

				for (int i = 0; i < info.num_files(); ++i) {
					memset(str, 0, 500);
					snprintf(str, sizeof(str),
						"{"
						"\"path\":\"%s\","
						"\"offset\":\"%d\","
						"\"hash\":\"%s\","
						"\"size\":\"%d\","
						"\"index\":\"%d\""
						"}"
						, info.file_at(i).path.c_str()
						, info.file_at(i).offset
						, libtorrent::to_hex(info.file_at(i).filehash.to_string()).c_str()
						, info.file_at(i).size
						, i
						);
					out += (comma + str);
					comma = ",";
				}
				out = "[" + out + "]";
			}
			result = out;
		}

	}
	catch (...){
		LOG_ERR("get ip error");
	}
	return result;
}

std::vector<int> DownloadSession::getPiecePriority(std::string& taskId){
	std::vector<int> result;
	try {
		libtorrent::torrent_handle pTorrent = getTorrentHandleFromTaskId(taskId);
		if (pTorrent.is_valid()){
			if (pTorrent.status().has_metadata) {
				libtorrent::torrent_info const& info = pTorrent.get_torrent_info();
				int pices_num = info.num_pieces();
				result = pTorrent.piece_priorities();
			}
		}
	}
	catch (...){
		LOG_ERR("Exception: failed to get pieces priority");

	}
	return result;
}
int DownloadSession::getPieceSize(std::string& taskId, int PieceIndex)
{
	int pieceSize = -1;
	try {

		libtorrent::torrent_handle pTorrent = getTorrentHandleFromTaskId(taskId);
		if (pTorrent.is_valid()){
			libtorrent::torrent_info const& info = pTorrent.get_torrent_info();
			int pices_num = info.num_pieces();
			if (PieceIndex < pices_num) {
				pieceSize = info.piece_size(PieceIndex);
			}
			else {
				LOG_ERR("LibTorrent.GetPieceSize not correct piece index");
			}
		}
	}
	catch (...){
		LOG_ERR("Exception: failed to get piece size");
	}
	return pieceSize;
}

std::map<std::string, int> DownloadSession::getFirstLastPiece(std::string& taskId, int fileIndex){
	std::map<std::string, int> result;
	try {
		libtorrent::torrent_handle pTorrent = getTorrentHandleFromTaskId(taskId);
		if (pTorrent.is_valid()){
			libtorrent::torrent_info const& info = pTorrent.get_torrent_info();
			//piece_num
			int pices_num = info.num_pieces();
			//piece大小
			int piece_size = info.piece_size(0);

			int first = int(info.file_at(fileIndex).offset / piece_size);
			int last = int(std::ceil(info.file_at(fileIndex).offset + info.file_at(fileIndex).size / piece_size));

			result.insert(std::pair<std::string, int>("first", first));
			result.insert(std::pair<std::string, int>("last", last));

		}
	}
	catch (...){
		LOG_ERR("Exception: failed to get piece size");
	}
	return result;
}
bool DownloadSession::setPiecePriorities(std::string& taskId, std::vector<int> priorities){
	bool result = false;
	try {
		libtorrent::torrent_handle pTorrent = getTorrentHandleFromTaskId(taskId);
		if (pTorrent.is_valid()){
			if (pTorrent.has_metadata()) {
				libtorrent::torrent_info const& info = pTorrent.get_torrent_info();
				int pieces_num = info.num_pieces();
				int arr_size = priorities.size();
				if (pieces_num == arr_size){
					pTorrent.prioritize_pieces(priorities);
					result = true;
				}
				else {
					LOG_ERR("LibTorrent.SetPiecePriorities priority array size failed");
				}
			}
		}
	}
	catch (...){
		LOG_ERR("Exception: failed to set pieces priority");
	}
	return result;
}
