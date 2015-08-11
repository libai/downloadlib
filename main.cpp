#include <stdlib.h>
#include <string.h>
#include "download.hpp"

#include "define.h" //全局定义


using namespace libtorrent;

int main(int argc, char* argv[])
{
	//启动下载进程

	std::string tempPath("E:\\c\\libtorrent-rasterbar-1.0.4\\torrent-test\\temp");
	DownloadSession* downloadSession = DownloadSession::instance();

	std::string downPath("E:\\c\\libtorrent-rasterbar-1.0.4\\torrent-test\\down");
	downloadSession->setSessionDir(tempPath, downPath);

	downloadSession->startSession( 0, 0, 0);
	LOG_INFO("启动下载进程成功");

	
	//添加下载任务
	std::string torrentPath("E:\\c\\libtorrent-rasterbar-1.0.4\\torrent-test\\3333.torrent");

	libtorrent::torrent_handle result;
	std::string info = "";
	std::string files = "";
	downloadSession->addTorrentFromFile(torrentPath);

	std::string httpUrl = "https://yts.to/torrent/download/D42234622CE77E59A9C4EEB41DCD2C8E6B59AC62.torrent";
	int status = 0;


	downloadSession->addTorrentFromUrl(httpUrl);

	std::string magnet = "magnet:?xt=urn:btih:DE91C4A7F183C4164BCD4549C933FB2A5F720E7A&dn=%E5%93%A5%E6%96%AF%E6%8B%89.Godzilla.2014.TC720P.X264.AAC.english.CHS.Mp4Ba";

	downloadSession->addTorrentFromUrl(magnet);

	int j = 0;
	//等待任务准备好
	while ((status == 11 || status<3) && j<100){

		status = downloadSession->getTaskStatus(httpUrl);
		LOG_INFO("status:%d", status);
		Sleep(1000);
		j++;
	}

	//文件
	std::string torrentFiles = downloadSession->getTorrentFiles(httpUrl);
	LOG_INFO("torrentFiles:%s", torrentFiles.c_str());
	
	//获取权重
	std::vector<int> priority = downloadSession->getPiecePriority(httpUrl);

	//视频文件头部与尾部权重高
	int piectSize = downloadSession->getPieceSize(httpUrl, 0);
	LOG_INFO("piectSize:%d", piectSize);
	int fileIndex = 0;

	std::map<std::string, int> firstLastPiece = downloadSession->getFirstLastPiece(httpUrl, fileIndex);

	LOG_INFO("first:%d", firstLastPiece["first"]);
	LOG_INFO("last:%d", firstLastPiece["last"]);

	int pices_num = priority.size();
	std::string out;
	for (int i = 0; i < pices_num; i++) {

		if (i == firstLastPiece["last"] || i == firstLastPiece["first"]){
			priority[i] = 7;
			out += "7";
		}
		else if ((i - firstLastPiece["first"])<10 && (i - firstLastPiece["first"])>0){
			priority[i] = 7;
			out += "6";
		}
		else{
			out += "0";
		}
	}
	LOG_INFO("PiecePriorities:%s", out.c_str());

	//重设权重
	downloadSession->setPiecePriorities(httpUrl, priority);

	int i = 0;
	while (i<100){


		std::string session_status = downloadSession->getSessionStatusJson();
		LOG_INFO("session_status:%s", session_status.c_str());

		std::string session_detail = downloadSession->getSessionDetailJson();
		LOG_INFO("session_detail:%s", session_detail.c_str());

		status = downloadSession->getTaskStatus(httpUrl);
		LOG_INFO("status:%d", status);
		//downloadSession->readAlerts();
		Sleep(10000);
		i++;
	}
	downloadSession->stopSession();
	/*
	if (hash == ""){
		LOG_INFO("添加任务失败");
		downloadSession->stopSession();
	}
	
	//获取文件信息
	files = download_LibTorrent_GetTorrentFiles(gSession, hash);
	LOG_INFO(files.c_str());

	//获取权重
	std::vector<int> priority = download_LibTorrent_GetPiecePriority(gSession, hash);

	//视频文件头部与尾部权重高
	download_LibTorrent_GetPieceSize(gSession, hash, 0);

	int fileIndex = 0;
	int firstLastPiece[2];
	download_LibTorrent_GetFirstLastPiece(gSession, hash, fileIndex, firstLastPiece);

	LOG_INFO("first:%d", firstLastPiece[0]);
	LOG_INFO("last:%d", firstLastPiece[1]);

	int pices_num = priority.size();
	std::string out;
	for (int i = 0; i < pices_num; i++) {
		
		if (i == firstLastPiece[1] || i == firstLastPiece[0]){
			priority[i] = 7;
			out += "7";
		}
		else if ((i - firstLastPiece[0])<10 && (i - firstLastPiece[0])>0){
			priority[i] = 7;
			out += "6";
		}else{
			out += "0";
		}
	}
	LOG_INFO("PiecePriorities:%s", out.c_str());
	
	//重设权重
	download_LibTorrent_SetPiecePriorities(gSession, hash, priority);

	while (1){
		//获取下载进度
		//info = download_LibTorrent_GetTorrentDetail(hash);
		//LOG_INFO(info.c_str());
		print_info(gSession, hash);
		Sleep(10000);


		//download_LibTorrent_PopAlerts();
	}
	download_LibTorrent_AbortSession(gSession);
	*/
}

