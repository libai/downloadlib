#include "define.h"

using namespace libtorrent;

enum task_state_t
{
	add_to_queued = 11,          //11, �¼������
	finish_all = 12,              //12, ���
	pause = 13,              //14, ��ͣ
	download_suspend = 14          //14���ж�
};
class TaskInfo{
public:
	std::string taskId;   //�����ʶ
	int status;             //0-7 
	std::string hash;      //infohash
	libtorrent::torrent_handle handle; //handle
};


class DownloadSession{

	private:
	explicit DownloadSession();
	static DownloadSession* m_instance;
	static volatile bool gSessionState;
	std::string tempDir;//��ʱĿ¼
	std::string downloadDir;
	libtorrent::session *s;
	std::string resumeDir;//��������Ŀ¼
	std::string torrentDir;//�����ļ�Ŀ¼

	std::map<std::string, TaskInfo*> tasks;
	int StorageMode = 0;

	public:
		static DownloadSession* instance();
		static void drop();
		~DownloadSession();
		torrent_handle getTorrentHandleFromHash(std::string &hash) const; //ȡ��handle
		torrent_handle getTorrentHandleFromTaskId(std::string &taskId); //ȡ��handle
		std::vector<torrent_handle> getTorrents() const;
		bool isFilePreviewPossible(std::string &hash) const;     //�ļ��Ƿ��Ԥ��
		//session����
		bool setSessionDir(std::string tempDir, std::string downDir);
		bool startSession(int ListenPort, int UploadLimit, int DownloadLimit);
		
		bool stopSession();
		inline libtorrent::session* getSession() const { return s; }
		
		std::string getSessionStatusJson();

		std::string getSessionDetailJson();



		//�������
		bool addTorrentFromUrl(std::string &url);

		bool addTorrentFromFile(std::string &torrentFile);


		//��ȡ�ļ���Ϣ
		std::string getTorrentFiles(std::string& taskId);

		//��ȡȨ��
		std::vector<int> getPiecePriority(std::string& taskId);

		//��ȡpiece���ļ���С
		int getPieceSize(std::string& taskId, int PieceIndex);
		//��ȡ��һ�������һ��picece��index
		std::map<std::string, int> getFirstLastPiece(std::string& taskId, int fileIndex);
		//����Ȩ��
		bool setPiecePriorities(std::string& taskId, std::vector<int> priorities);



		//void recheckTorrent(std::string &taskId); //�������������
		
		void deleteTorrent(std::string &taskId, bool delete_local_files = false);
		
		void pauseAllTorrents();
		void pauseTorrent(std::string &hash);
		void suspendOtherTorrents(std::string &hash);
		void resumeTorrent(std::string &hash, const bool force = false);
		void resumeSuspendTorrents(const bool force = false);

		void handleAlert(libtorrent::alert* a);
		
		void readAlerts();
		void print_alert(libtorrent::alert const* a, std::string& str);

		//task
		int getTaskStatus(std::string &taskId);
		std::string getTasksJson(std::string status);
		libtorrent::torrent_handle get_torrent_handle_from_hash(std::string& hash); 

	private:
	
		bool loadFastResumeData(std::string &hash, std::vector<char> &buf);
		
		void loadTorrentTempData(torrent_handle &h, std::string savePath, bool magnet);
		
		int loadFile(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit = 8000000);
		int saveFile(std::string const& filename, std::vector<char>& v);
		void saveState();
		void loadState();
		void handleAddTorrentAlert(libtorrent::add_torrent_alert* p);

		void handleTorrentFinishedAlert(libtorrent::torrent_finished_alert* p);
		void handleMetadataReceivedAlert(libtorrent::metadata_received_alert* p);
		void handleSaveResumeDataAlert(libtorrent::save_resume_data_alert* p);
		void handleTorrentPausedAlert(libtorrent::torrent_paused_alert* p);
		void handleStateUpdateAlert(libtorrent::state_update_alert *p);

		//task
		void setTask(std::string& taskId, int status, libtorrent::torrent_handle& handle);
		void setTask(std::string& taskId, int status, std::string& hash);
		void saveTasks(); //save to file
		TaskInfo* getTaskByHandle(libtorrent::torrent_handle& handle);
		libtorrent::torrent_handle addTorrentFromTask(std::string torrentFile);

		void loadTasks(); //load from file
		//void removeTask(std::string& taskId);

};
