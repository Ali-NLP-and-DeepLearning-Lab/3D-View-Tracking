//   Copyright (C) 2015  Marek Kowalski (M.Kowalski@ire.pw.edu.pl), Jacek Naruniec (J.Naruniec@ire.pw.edu.pl)
//   License: MIT Software License   See LICENSE.txt for the full license.

//   If you use this software in your research, then please use the following citation:

//    Kowalski, M.; Naruniec, J.; Daniluk, M.: "LiveScan3D: A Fast and Inexpensive 3D Data
//    Acquisition System for Multiple Kinect v2 Sensors". in 3D Vision (3DV), 2015 International Conference on, Lyon, France, 2015

//    @INPROCEEDINGS{Kowalski15,
//        author={Kowalski, M. and Naruniec, J. and Daniluk, M.},
//        booktitle={3D Vision (3DV), 2015 International Conference on},
//        title={LiveScan3D: A Fast and Inexpensive 3D Data Acquisition System for Multiple Kinect v2 Sensors},
//        year={2015},
//    }
#include "stdafx.h"
#include "resource.h"
#include "LiveScanClient.h"
#include "filter.h"
#include <chrono>
#include <strsafe.h>
#include <fstream>
#include "zstd.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "yolo_v2_class.hpp"
#pragma warning(disable:4996)

#define PORT 8901
#define IP_ADDRESS "192.168.43.90"

// coco 80�� label �� �ٿ� 10����, 0~79
std::vector<char*> Objectlabel =
{ "person","bicycle","car","motorbike","aeroplane","bus","train","truck","boat","traffic light",
"fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow",
"elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
"skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle",
"wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange",
"broccoli","carrot","hot dog","pizza","donut","cake","chair","sofa","pottedplant","bed",
"diningtable","toilet","monitor","laptop","mouse","remote","keyboard","remote","microwave","oven",
"toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush" };

//clock_t t1 = clock();
//clock_t t2 = t1;

void send_message(int maxIdx) {
	WSADATA wsaData;
	SOCKET hSocket;
	SOCKADDR_IN servAddr;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		printf("startup error\n");

	// host socket (local)
	hSocket = socket(PF_INET, SOCK_STREAM, 0);
	if (hSocket == INVALID_SOCKET)
		printf("invalid socket\n");

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.S_un.S_addr = inet_addr(IP_ADDRESS);
	servAddr.sin_port = htons(PORT);

	// connect to server ( server - JG )
	if (connect(hSocket, (SOCKADDR*)&servAddr, sizeof(servAddr)) == SOCKET_ERROR)
		printf("connection error\n");

	char* message;
	if (maxIdx == 0)
		message = "1";
	else
		message = "0";

	send(hSocket, message, sizeof(message), 0);

	printf("Closing socket \n");
	closesocket(hSocket);
	WSACleanup();

}

std::mutex m_mSocketThreadMutex;

int APIENTRY wWinMain(    
	_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
    )
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LiveScanClient application;
    application.Run(hInstance, nShowCmd);
}

LiveScanClient::LiveScanClient() :
    m_hWnd(NULL),
    m_nLastCounter(0),
    m_nFramesSinceUpdate(0),
    m_fFreq(0),
    m_nNextStatusTime(0LL),
    m_pD2DFactory(NULL),
    m_pDrawColor(NULL),
	m_pDepthRGBX(NULL),
	m_pCameraSpaceCoordinates(NULL),
	m_pColorCoordinatesOfDepth(NULL),
	m_pDepthCoordinatesOfColor(NULL),
	m_bCalibrate(false),
	m_bFilter(false),
	m_bStreamOnlyBodies(false),
	m_bCaptureFrame(false),
	m_bConnected(false),
	m_bConfirmCaptured(false),
	m_bConfirmCalibrated(false),
	m_bShowDepth(false),
	m_bSocketThread(true),
	m_bFrameCompression(true),
	m_iCompressionLevel(2),
	m_pClientSocket(NULL),
	m_nFilterNeighbors(10),
	m_fFilterThreshold(0.01f)
{
	pCapture = new KinectCapture();

    LARGE_INTEGER qpf = {0};
    if (QueryPerformanceFrequency(&qpf))
    {
        m_fFreq = double(qpf.QuadPart);
    }

	m_vBounds.push_back(-0.5);
	m_vBounds.push_back(-0.5);
	m_vBounds.push_back(-0.5);
	m_vBounds.push_back(0.5);
	m_vBounds.push_back(0.5);
	m_vBounds.push_back(0.5);

	calibration.LoadCalibration();
}
  
LiveScanClient::~LiveScanClient()
{
    // clean up Direct2D renderer
    if (m_pDrawColor)
    {
        delete m_pDrawColor;
        m_pDrawColor = NULL;
    }

	if (pCapture)
	{
		delete pCapture;
		pCapture = NULL;
	}

	if (m_pDepthRGBX)
	{
		delete[] m_pDepthRGBX;
		m_pDepthRGBX = NULL;
	}

	if (m_pCameraSpaceCoordinates)
	{
		delete[] m_pCameraSpaceCoordinates;
		m_pCameraSpaceCoordinates = NULL;
	}

	if (m_pColorCoordinatesOfDepth)
	{
		delete[] m_pColorCoordinatesOfDepth;
		m_pColorCoordinatesOfDepth = NULL;
	}

	if (m_pDepthCoordinatesOfColor)
	{
		delete[] m_pDepthCoordinatesOfColor;
		m_pDepthCoordinatesOfColor = NULL;
	}

	if (m_pClientSocket)
	{
		delete m_pClientSocket;
		m_pClientSocket = NULL;
	}
    // clean up Direct2D
    SafeRelease(m_pD2DFactory);

}

int LiveScanClient::Run(HINSTANCE hInstance, int nCmdShow)
{
    MSG       msg = {0};
    WNDCLASS  wc;

	//Detector myDetector("yolo.cfg", "yolo.weights");
	//Detector myDetector("yolov3-608.cfg", "yolov3-608.weights");
	//Detector myDetector("yolov3-512.cfg", "yolov3-512.weights");
	Detector myDetector("yolov3-416.cfg", "yolov3-416.weights");
	//Detector myDetector("yolov3-320.cfg", "yolov3-320.weights");



	std::vector<bbox_t> bBoxes;
	frames = new cv::Mat(cv::Size(1920, 1080), CV_8UC4);
	
	clock_t before = 0, after = 0;



	/*

	cv::Mat *targetBooks, *targetHist;
	cv::Mat bookImg, bookHist;	
	bool manflag[3] = { false, false, false };

	
	targetBooks = new cv::Mat[3];
	targetHist = new cv::Mat[3];
	targetBooks[0] = cv::imread("../faces/brian.jpg");//18.05.07 : ����ȭ ���� ���Ŀ� ���� �κ��� �������ҵ� // 18.06.07 : �� ũ�� 256x256 å���� ������� �ٲ�. ���� 3�� ���� �Ǿ�����. 0���� ���� 1���� �մ�. 2���� �׳� �ְ� �Ⱦ�.
	targetBooks[1] = cv::imread("../faces/dennis.jpg");
	targetBooks[2] = cv::imread("../faces/ingyu.jpg");
	
	bool obj_flag = false;

	for (int a = 0; a < 3; a++)
	{
	cv::resize(targetBooks[a], targetBooks[a], cv::Size(64, 64));
	cv::normalize(targetBooks[a], targetBooks[a], 0, 255, cv::NORM_MINMAX, -1, cv::Mat());
	//cv::calcHist(&targetBooks[a], 1, channels, cv::Mat(), targetHist[a], 3, histSize, ranges, true, false); //18.05.07 : ����ȭ ���� ���Ŀ� ���� �κ��� �������ҵ�
	//cv::normalize(targetHist[a], targetHist[a], 0, 255, cv::NORM_MINMAX, -1, cv::Mat());
	}
	*/

	int channels[] = { 0, 1, 2 };
	int histSize[] = { 32, 32, 32 };
	float r_ranges[] = { 0, 256 };
	float g_ranges[] = { 0, 256 };
	float b_ranges[] = { 0, 256 };
	const float* ranges[] = { r_ranges, g_ranges, b_ranges };
		


	// Dialog custom window class
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"LiveScanClientAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
	HWND hWndApp = CreateDialogParamW(
        NULL,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC)LiveScanClient::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

    // Show window
    ShowWindow(hWndApp, nCmdShow);

	std::thread t1(&LiveScanClient::SocketThreadFunction, this);
    
	before = clock();
	// Main message loop
    while (WM_QUIT != msg.message)
    {
		//HandleSocket();
		UpdateFrame();
		
		memcpy(frames->data, pCapture->pColorRGBX, 1920 * 1080 * sizeof(RGB));


		cv::Mat test1(cv::Size(1920,1080), CV_8UC3);

		cv::cvtColor(*frames, test1, CV_BGRA2BGR);
		bBoxes = myDetector.detect(test1);
		/*
		IplImage *viewti = cvCreateImage(cvSize(pCapture->nColorFrameWidth, pCapture->nColorFrameHeight), 8, 3); //�̹��� ���� �������� �����ұ�??
																												 //IplImage *viewti=0;
		RGB* RGBim = pCapture->pColorRGBX;

		int dst1_height, dst1_width;
		uchar*dst1_data; // �����ٸ��� ����?

		dst1_height = viewti->height;
		dst1_width = viewti->widthStep;
		dst1_data = (uchar*)viewti->imageData;
		for (int i = 0; i<pCapture->nColorFrameWidth*pCapture->nColorFrameHeight; i++)
		{
			dst1_data[i * 3] = RGBim[i].rgbBlue;
			dst1_data[i * 3 + 1] = RGBim[i].rgbGreen;
			dst1_data[i * 3 + 2] = RGBim[i].rgbRed;
		}

		cv::Mat testim = cv::cvarrToMat(viewti);		
		bBoxes = myDetector.detect(testim);
		*/


//		obid[30][5] = { 0 }; // obid �ʱ�ȭ... �־�� �ұ�??
		for (int i = 0; i < 30; i++)
		{
			obid[i][0] = 0;
			obid[i][1] = 0;
			obid[i][2] = 0;
			obid[i][3] = 0;
			obid[i][4] = 0;
			obid[i][5] = 0;
			obid[i][6] = 0;
			obid[i][7] = 0;
			obid[i][8] = 0;

			viid[i][0] = 0;
			viid[i][1] = 0;
			viid[i][2] = 0;
			viid[i][3] = 0;
			viid[i][4] = 0;
			viid[i][5] = 0;
			viid[i][6] = 0;



		}

		// �� ���� ���� �ʱ�ȭ
		for (int fi = 0; fi < 6; fi++)
		{
			facd[fi][0] = 0;
			facd[fi][1] = 0;
			facd[fi][2] = 0;
			facd[fi][3] = 0;
			facd[fi][4] = 0;
			facd[fi][5] = 0;
			facd[fi][6] = 0;
			facd[fi][7] = 0;
			facd[fi][8] = 0;
			facd[fi][9] = 0;
		}



		int obcnt = 0; 

		std::vector<bbox_t>::iterator iter = bBoxes.begin();

		for (iter = bBoxes.begin(); iter != bBoxes.end(); ++iter)

		{
			int x1 = iter->x;
			int y1 = iter->y;
			int x2 = iter->x + iter->w;
			int y2 = iter->y + iter->h;
			int x3 = iter->x + (iter->w) / 2;
			int y3 = iter->y + (iter->h) / 2;


			//char text[255] = "";	// id �� Ȯ��



			//180511 : �ð� üũ

			time_t tt = time(0);
			struct tm * now = localtime(&tt);



			if (iter->prob > 0.3 && iter->obj_id != 60 && iter->obj_id != 56 && x3 >= 180 && x3 <= 1740)   //18.05.07 : depth �� RGB ���̰� �뷫������ + - 180 �ȼ� => ���Ŀ� Ű��Ʈ �Լ� �̿��ؼ� �����ϰ� ������� �ٰ�
			{
				//cv::rectangle(test1, cvPoint(x1, y1), cvPoint(x2, y2), CV_RGB(0, 0, 255), 3);
				////sprintf(text, "id = %d, prob = %f", iter->obj_id, iter->prob);
				//sprintf(text, "name = %s, prob = %f", Objectlabel[iter->obj_id], iter->prob);
				//cv::putText(test1, text, cvPoint(x1, y1), 2, 1.2, cv::Scalar::all(255));

				//18.05.07 3d ��ǥ ��Ī�ϴ� �κ� => for�� �ּ��Ͻ�Ű�� ��� ��� �ʿ�
				float tpd = 5000;
				float tdb = 0;
				int ttdb = 0;

				for (unsigned int i = 0; i < m_vLastFrameimp.size(); i++)  //18.05.07 : �̺κ� ����� �ʿ�...
				{
					tdb = sqrt((m_vLastFrameimp[i].X - x3)*(m_vLastFrameimp[i].X - x3) + (m_vLastFrameimp[i].Y - y3)*(m_vLastFrameimp[i].Y - y3));

					if (tdb < tpd) //�ּҰ� ã��
					{
						tpd = tdb;
						ttdb = i;
					}
				}

				if (tpd < 50) // �ּ� �Ÿ� �ȼ� 75 �̳��� �� 3d ��ǥ ����Ѵ�. , 6�� ��ũ�� �������� 150 ���� .... ���� ���� �߽��� ������ �� �� ������ҵ� //190414 : 75 -> 50
				{
					obid[obcnt][0] = 1; // ������ ���°� üũ?
					obid[obcnt][1] = iter->obj_id; // ������Ʈ id
					obid[obcnt][2] = m_vLastFrameVertices[ttdb].X; // ������Ʈ X ��ǥ
					obid[obcnt][3] = m_vLastFrameVertices[ttdb].Y; // ������Ʈ Y ��ǥ
					obid[obcnt][4] = m_vLastFrameVertices[ttdb].Z; // ������Ʈ Z ��ǥ
					obid[obcnt][5] = 1; //Ű��Ʈ id  
					obid[obcnt][6] = now->tm_hour; //��
					obid[obcnt][7] = now->tm_min; //��
					obid[obcnt][8] = now->tm_sec; //��


					viid[obcnt][0] = iter->x;
					viid[obcnt][1] = iter->y;
					viid[obcnt][2] = iter->w;
					viid[obcnt][3] = iter->h;
					viid[obcnt][4] = iter->obj_id;
					viid[obcnt][5] = iter->prob;


					/*
					cv::rectangle(test1, cvPoint(x1, y1), cvPoint(x2, y2), CV_RGB(0, 0, 255), 3);
					//sprintf(text, "id = %d, prob = %f", iter->obj_id, iter->prob);
					sprintf(text, "name = %s, prob = %0.2f", Objectlabel[iter->obj_id], iter->prob);
					//sprintf(text, "name = %s, prob = %f, dis = %f", Objectlabel[iter->obj_id], iter->prob, tpd);
					cv::putText(test1, text, cvPoint(x1, y1), 2, 1.2, cv::Scalar::all(255));
					*/
					obcnt++; //���ǹ� �����ϴ� ī���� , 30�� �̻� ���� �ڻ쳿
					if (obcnt == 29)
					{
						break;
					}


				}

				/*
				char texto[255] = "";	// �ʱ�ȭ Ȯ��

				int nobid = 0;               //Faces.size();     // �󱼰���

				for (int i = 0; i < 30; i++)
				{
					if (obid[i][0] == 1)
					{
						nobid++;
					}
				}



				sprintf(texto, "num = %d", nobid);
				cv::putText(test1, texto, cvPoint(100, 100), 2, 1.2, cv::Scalar::all(255));
				*/

			}




		}
		

		// �ü��� ������ �ּҰŸ� üũ

		if (pcheck == 1)
		{
			for (int i = 0; i < hunum; i++)
			{

				float dispc = 0;
				float dispcc = 5000;
				int cknum = 0;

				for (int j = 0; j < obcnt; j++)
				{

					dispc = sqrt((pview[i].X - viid[j][0]) * (pview[i].X - viid[j][0]) + (pview[i].Y - viid[j][1]) * (pview[i].Y - viid[j][1]));
					if (dispc < dispcc) //�ּҰ� ã��
					{
						dispcc = dispc;                         //180530 ���� �� ���� �ִ��� , �ٿ�� �ڽ� ����� ����� ������
						cknum = j;
					}

				}
				if (dispcc < 215) //150? 200? 250? => ���� �̰͵� �ü� ������ �Ÿ��� ���� ���������� ����
				{
					viid[cknum][6] = 1;
				}

			}

		}

		// ��� �� �� �߰�
		for (int i = 0; i < BODY_COUNT; i++)
		{



			if (pCapture->FaceData[i][0] == 1)
			{


				facd[i][0] = pCapture->FaceData[i][0];
				facd[i][1] = pCapture->FaceData[i][1];
				facd[i][2] = pCapture->FaceData[i][2];
				facd[i][3] = pCapture->FaceData[i][3];
				facd[i][4] = pCapture->FaceData[i][4];
				facd[i][5] = pCapture->FaceData[i][5];
				facd[i][6] = pCapture->FaceData[i][6];
				facd[i][7] = pCapture->FaceData[i][7];
				facd[i][8] = pCapture->FaceData[i][8];
				facd[i][9] = pCapture->FaceData[i][9];


			}


			/*
				// int -> float
				float hexx = pCapture->FaceData[i][1] / 10; // image �� x��ǥ
				float heyy = pCapture->FaceData[i][2] / 10; // image �� y��ǥ


				int facewidth = 0;
				int faceheight = 0;
				int centx = 0;
				int centy = 0;


			for (int j = 0; j < obcnt; j++)
			{


				facewidth = viid[j][2] / 3;
				faceheight = viid[j][3] / 3;
				centx = viid[j][0] + viid[j][2] / 2;
				centy = viid[j][1] + viid[j][3] / 2;
				if (!strcmp(Objectlabel[viid[j][4]], "person") && abs(centx - hexx) < 50)
				{

					bookImg = cv::Mat(test1, cv::Rect(cvPoint(hexx - facewidth / 2, heyy - facewidth / 2), cvPoint(hexx + facewidth / 2, heyy + facewidth / 2)));
					cv::resize(bookImg, bookImg, cv::Size(64, 64));
					cv::normalize(bookImg, bookImg, 0, 255, cv::NORM_MINMAX, -1, cv::Mat());

					int maxman = INT_MAX;
					int lastman = -2;
					for (int man = 0; man < 2; man++) //�̰� �ִ� ������ ���� ��� ��. 0���� ����, 1���� �մ����� �ϴ� ����.
					{
						int mandist = cv::norm(bookImg, targetBooks[man], CV_L1);
						if (mandist < maxman)
						{
							maxman = mandist;
							lastman = man;
						}
					}
					pCapture->FaceData[i][9] = lastman + 1;







				if(facd[i][9] == 1)
				{
					cv::rectangle(test1, cvPoint(hexx - facewidth / 2, heyy - facewidth / 2), cvPoint(hexx + facewidth / 2, heyy + facewidth / 2), CV_RGB(0, 255, 0), 3); //���
				}
				else if(facd[i][9] == 2)
				{
					cv::rectangle(test1, cvPoint(hexx - facewidth / 2, heyy - facewidth / 2), cvPoint(hexx + facewidth / 2, heyy + facewidth / 2), CV_RGB(0, 255, 255), 3); //û�ϻ�
				}
				else if (facd[i][9] == 3)
				{
					cv::rectangle(test1, cvPoint(hexx - facewidth / 2, heyy - facewidth / 2), cvPoint(hexx + facewidth / 2, heyy + facewidth / 2), CV_RGB(255, 255, 0), 3); //�����
				}
				else
				{
					cv::rectangle(test1, cvPoint(hexx - facewidth / 2, heyy - facewidth / 2), cvPoint(hexx + facewidth / 2, heyy + facewidth / 2), CV_RGB(148, 0, 211), 3); //�����
				}
				// bookImg release ���߿� �ֱ�

				*/


				//if (!manflag[0])
				//{
				//	pCapture->FaceData[i][7] = 1;
				//	//Objectlabel[viid[j][4]] = "Owner";
				//	manflag[0] = true;
				//}
				//else if(!manflag[1])
				//{
				//	pCapture->FaceData[i][7] = 2;
				//	//Objectlabel[viid[j][4]] = "Friend";
				//	manflag[1] = true;
				//}
				//else if(!manflag[2])
				//{
				//	pCapture->FaceData[i][7] = 3;
				//	//Objectlabel[viid[j][4]] = "Friend2";
				//	manflag[2] = true;
				//}
				////else
				//	//Objectlabel[viid[j][4]] = "Who are you?";

		}







		


			//pCapture->FaceData[i][7] = 1; // ����
			//pCapture->FaceData[i][7] = 2; // �Խ�Ʈ



		// üũ�ؼ� ���

		for (int j = 0; j < obcnt; j++)
		{
			char text[255] = "";	// id �� Ȯ�� 

			if (viid[j][6] == 1)
			{
				cv::rectangle(test1, cvPoint(viid[j][0], viid[j][1]), cvPoint(viid[j][0]+ viid[j][2], viid[j][1]+ viid[j][3]), CV_RGB(255, 0, 0), 3);
				//sprintf(text, "id = %d, prob = %f", iter->obj_id, iter->prob);
				//sprintf(text, "name = %s, prob = %0.2f", Objectlabel[viid[j][4]], viid[j][5]);

				sprintf(text, "%s", Objectlabel[viid[j][4]]);

				//sprintf(text, "name = %s, prob = %f, dis = %f", Objectlabel[iter->obj_id], iter->prob, tpd);
				//cv::putText(test1, text, cvPoint(viid[j][0], viid[j][1]), 2, 1.2, cv::Scalar::all(255));

				cv::putText(test1, text, cvPoint(viid[j][0], viid[j][1]), 2, 2.5, CV_RGB(255, 0, 0),3);
			}
			else
			{
				cv::rectangle(test1, cvPoint(viid[j][0], viid[j][1]), cvPoint(viid[j][0] + viid[j][2], viid[j][1] + viid[j][3]), CV_RGB(0, 0, 255), 3);
				//sprintf(text, "id = %d, prob = %f", iter->obj_id, iter->prob);
				//sprintf(text, "name = %s, prob = %0.2f", Objectlabel[viid[j][4]], viid[j][5]);

				sprintf(text, "%s", Objectlabel[viid[j][4]]);

				//sprintf(text, "name = %s, prob = %f, dis = %f", Objectlabel[iter->obj_id], iter->prob, tpd);
				//cv::putText(test1, text, cvPoint(viid[j][0], viid[j][1]), 2, 1.2, cv::Scalar::all(255));

				cv::putText(test1, text, cvPoint(viid[j][0], viid[j][1]), 2, 2.5, CV_RGB(255, 0, 0),3);

			}
		}



		

	

		/*
		//�� ��� �κ� 180530 => �̰� ������� ���� ��� �ڵ����� �پ� �ִ°� ������?? ������ �ƴ� ���ÿ��� ���̸� ��� �����ҵ�
		for(int ii=0 ; ii < 6; ii++)
		{
			if (pCapture->FaceBox[ii][1] != 0)
			{
				if(ii == 0)
				{ 
				cv::rectangle(test1, cvPoint(pCapture->FaceBox[ii][1], pCapture->FaceBox[ii][3]), cvPoint(pCapture->FaceBox[ii][2], pCapture->FaceBox[ii][0]), CV_RGB(0, 255, 0), 3);
				}
				else if (ii == 1)
				{
				cv::rectangle(test1, cvPoint(pCapture->FaceBox[ii][1], pCapture->FaceBox[ii][3]), cvPoint(pCapture->FaceBox[ii][2], pCapture->FaceBox[ii][0]), CV_RGB(0, 0, 255), 3);
				}
				else if (ii == 2)
				{
					cv::rectangle(test1, cvPoint(pCapture->FaceBox[ii][1], pCapture->FaceBox[ii][3]), cvPoint(pCapture->FaceBox[ii][2], pCapture->FaceBox[ii][0]), CV_RGB(0, 255, 255), 3);
				}
				else if (ii == 3)
				{
					cv::rectangle(test1, cvPoint(pCapture->FaceBox[ii][1], pCapture->FaceBox[ii][3]), cvPoint(pCapture->FaceBox[ii][2], pCapture->FaceBox[ii][0]), CV_RGB(0, 255, 125), 3);
				}
				else if (ii == 4)
				{
					cv::rectangle(test1, cvPoint(pCapture->FaceBox[ii][1], pCapture->FaceBox[ii][3]), cvPoint(pCapture->FaceBox[ii][2], pCapture->FaceBox[ii][0]), CV_RGB(0, 125, 255), 3);
				}
				else if (ii == 5)
				{
					cv::rectangle(test1, cvPoint(pCapture->FaceBox[ii][1], pCapture->FaceBox[ii][3]), cvPoint(pCapture->FaceBox[ii][2], pCapture->FaceBox[ii][0]), CV_RGB(0, 125, 125), 3);
				}
				//�� ã�°� ���??	
			}
		}
		*/

		cv::cvtColor(test1, *frames, CV_BGR2BGRA); // 18.05.07 : �̰� ������?? => opencv�� ó���� �̹����� ���� �̹����� ������� && frames�� release??
		
		test1.release();		




        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // If a dialog message will be taken care of by the dialog proc
            if (hWndApp && IsDialogMessageW(hWndApp, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

	m_bSocketThread = false;
	t1.join();
    return static_cast<int>(msg.wParam);
}



void LiveScanClient::UpdateFrame()
{
	if (!pCapture->bInitialized)
	{
		return;
	}

	bool bNewFrameAcquired = pCapture->AcquireFrame();

	if (!bNewFrameAcquired)
		return;

	pCapture->MapDepthFrameToCameraSpace(m_pCameraSpaceCoordinates);
	pCapture->MapDepthFrameToColorSpace(m_pColorCoordinatesOfDepth);
	{
		std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);
		StoreFrame(m_pCameraSpaceCoordinates, m_pColorCoordinatesOfDepth, pCapture->pColorRGBX, pCapture->vBodies, pCapture->pBodyIndex);

		if (m_bCaptureFrame)
		{
			m_framesFileWriterReader.writeFrame(m_vLastFrameVertices, m_vLastFrameRGB);
			m_bConfirmCaptured = true;
			m_bCaptureFrame = false;
		}
	}

	if (m_bCalibrate)
	{		
		std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);
		Point3f *pCameraCoordinates = new Point3f[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
		pCapture->MapColorFrameToCameraSpace(pCameraCoordinates);

		bool res = calibration.Calibrate(pCapture->pColorRGBX, pCameraCoordinates, pCapture->nColorFrameWidth, pCapture->nColorFrameHeight);

		delete[] pCameraCoordinates;

		if (res)
		{
			m_bConfirmCalibrated = true;
			m_bCalibrate = false;
		}
	}

	if (!m_bShowDepth)
		//ProcessColor(pCapture->pColorRGBX, pCapture->nColorFrameWidth, pCapture->nColorFrameHeight);///////Important
		ProcessColor((RGB*)frames->data, pCapture->nColorFrameWidth, pCapture->nColorFrameHeight);
	else
		ProcessDepth(pCapture->pDepth, pCapture->nDepthFrameWidth, pCapture->nDepthFrameHeight);

	ShowFPS();
}

LRESULT CALLBACK LiveScanClient::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LiveScanClient* pThis = NULL;
    
    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<LiveScanClient*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<LiveScanClient*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

LRESULT CALLBACK LiveScanClient::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
        case WM_INITDIALOG:
        {
            // Bind application window handle
            m_hWnd = hWnd;

            // Init Direct2D
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

            // Get and initialize the default Kinect sensor
			bool res = pCapture->Initialize();
			if (res)
			{
				m_pDepthRGBX = new RGB[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];

				m_pCameraSpaceCoordinates = new Point3f[pCapture->nDepthFrameWidth * pCapture->nDepthFrameHeight];
				m_pColorCoordinatesOfDepth = new Point2f[pCapture->nDepthFrameWidth * pCapture->nDepthFrameHeight];
				m_pDepthCoordinatesOfColor = new Point2f[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
			}
			else
			{
				SetStatusMessage(L"Capture device failed to initialize!", 10000, true);
			}

			// Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
			// We'll use this to draw the data we receive from the Kinect to the screen
			HRESULT hr;
			m_pDrawColor = new ImageRenderer();
			hr = m_pDrawColor->Initialize(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), m_pD2DFactory, pCapture->nColorFrameWidth, pCapture->nColorFrameHeight, pCapture->nColorFrameWidth * sizeof(RGB));
			if (FAILED(hr))
			{
				SetStatusMessage(L"Failed to initialize the Direct2D draw device.", 10000, true);
			}

			ReadIPFromFile();
        }
        break;

        // If the titlebar X is clicked, destroy app
		case WM_CLOSE:	
			WriteIPToFile();
			DestroyWindow(hWnd);						 
			break;
        case WM_DESTROY:
            // Quit the main message pump
            PostQuitMessage(0);
            break;
			
        // Handle button press
        case WM_COMMAND:
			if (IDC_BUTTON_CONNECT == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
			{
				std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);
				if (m_bConnected)
				{
					delete m_pClientSocket;
					m_pClientSocket = NULL;

					m_bConnected = false;
					SetDlgItemTextA(m_hWnd, IDC_BUTTON_CONNECT, "Connect");
				}
				else
				{
					try
					{
						char address[20];
						GetDlgItemTextA(m_hWnd, IDC_IP, address, 20);
						m_pClientSocket = new SocketClient(address, 48001);

						m_bConnected = true;
						if (calibration.bCalibrated)
							m_bConfirmCalibrated = true;

						SetDlgItemTextA(m_hWnd, IDC_BUTTON_CONNECT, "Disconnect");
						//Clear the status bar so that the "Failed to connect..." disappears.
						SetStatusMessage(L"", 1, true);
					}
					catch (...)
					{
						SetStatusMessage(L"Failed to connect. Did you start the server?", 10000, true);
					}
				}
			}
			if (IDC_BUTTON_SWITCH == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
			{
				m_bShowDepth = !m_bShowDepth;

				if (m_bShowDepth)
				{
					SetDlgItemTextA(m_hWnd, IDC_BUTTON_SWITCH, "Show color");
				}
				else
				{
					SetDlgItemTextA(m_hWnd, IDC_BUTTON_SWITCH, "Show depth");
				}
			}
            break;
    }

    return FALSE;
}

void LiveScanClient::ProcessDepth(const UINT16* pBuffer, int nWidth, int nHeight)
{
	// Make sure we've received valid data
	if (m_pDepthRGBX && m_pDepthCoordinatesOfColor && pBuffer && (nWidth == pCapture->nDepthFrameWidth) && (nHeight == pCapture->nDepthFrameHeight))
	{
		// end pixel is start + width*height - 1
		const UINT16* pBufferEnd = pBuffer + (nWidth * nHeight);

		pCapture->MapColorFrameToDepthSpace(m_pDepthCoordinatesOfColor);

		for (int i = 0; i < pCapture->nColorFrameWidth * pCapture->nColorFrameHeight; i++)
		{
			Point2f depthPoint = m_pDepthCoordinatesOfColor[i];
			BYTE intensity = 0;
			
			if (depthPoint.X >= 0 && depthPoint.Y >= 0)
			{
				int depthIdx = (int)(depthPoint.X + depthPoint.Y * pCapture->nDepthFrameWidth);
				USHORT depth = pBuffer[depthIdx];
				intensity = static_cast<BYTE>(depth % 256);
			}

			m_pDepthRGBX[i].rgbRed = intensity;
			m_pDepthRGBX[i].rgbGreen = intensity;
			m_pDepthRGBX[i].rgbBlue = intensity;
		}

		// Draw the data with Direct2D
		m_pDrawColor->Draw(reinterpret_cast<BYTE*>(frames->data), pCapture->nColorFrameWidth * pCapture->nColorFrameHeight * sizeof(RGB), pCapture->vBodies);
		//m_pDrawColor->Draw(reinterpret_cast<BYTE*>(m_pDepthRGBX), pCapture->nColorFrameWidth * pCapture->nColorFrameHeight * sizeof(RGB), pCapture->vBodies);//////////Important
	}
}

void LiveScanClient::ProcessColor(RGB* pBuffer, int nWidth, int nHeight) 
{

    // Make sure we've received valid data
	if (pBuffer && (nWidth == pCapture->nColorFrameWidth) && (nHeight == pCapture->nColorFrameHeight))
    {
		if(pcheck ==1) // ����Ʈ ������ ��� �����
		{ 
		// Draw the data with Direct2D

				m_pDrawColor->Drawp(reinterpret_cast<BYTE*>(pBuffer), pCapture->nColorFrameWidth * pCapture->nColorFrameHeight * sizeof(RGB), pview, hunum); // 180414 for���� ������ �����Ÿ� => �ѹ��� �� �־ draw �ؾ���

		}
		else
		{
		// Draw the data with Direct2D
		m_pDrawColor->Draw(reinterpret_cast<BYTE*>(pBuffer), pCapture->nColorFrameWidth * pCapture->nColorFrameHeight * sizeof(RGB), pCapture->vBodies);
		}

    }
}

bool LiveScanClient::SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce)
{
    INT64 now = GetTickCount64();

    if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
    {
        SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
        m_nNextStatusTime = now + nShowTimeMsec;

        return true;
    }

    return false;
}

void LiveScanClient::SocketThreadFunction()
{
	while (m_bSocketThread)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		HandleSocket();
	}
}

void LiveScanClient::HandleSocket()
{
	char byteToSend;
	std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);

	if (!m_bConnected)
	{
		return;
	}

	string received = m_pClientSocket->ReceiveBytes();
	for (unsigned int i = 0; i < received.length(); i++)
	{
		//capture a frame
		if (received[i] == MSG_CAPTURE_FRAME)
			m_bCaptureFrame = true;
		//calibrate
		else if (received[i] == MSG_CALIBRATE)
			m_bCalibrate = true;
		//receive settings
		//TODO: what if packet is split?
		else if (received[i] == MSG_RECEIVE_SETTINGS)
		{
			vector<float> bounds(6);
			i++;
			int nBytes = *(int*)(received.c_str() + i);
			i += sizeof(int);

			for (int j = 0; j < 6; j++)
			{
				bounds[j] = *(float*)(received.c_str() + i);
				i += sizeof(float);
			}
				
			m_bFilter = (received[i]!=0);
			i++;

			m_nFilterNeighbors = *(int*)(received.c_str() + i);
			i += sizeof(int);

			m_fFilterThreshold = *(float*)(received.c_str() + i);
			i += sizeof(float);

			m_vBounds = bounds;

			int nMarkers = *(int*)(received.c_str() + i);
			i += sizeof(int);

			calibration.markerPoses.resize(nMarkers);

			for (int j = 0; j < nMarkers; j++)
			{
				for (int k = 0; k < 3; k++)
				{
					for (int l = 0; l < 3; l++)
					{
						calibration.markerPoses[j].R[k][l] = *(float*)(received.c_str() + i);
						i += sizeof(float);
					}
				}

				for (int k = 0; k < 3; k++)
				{
					calibration.markerPoses[j].t[k] = *(float*)(received.c_str() + i);
					i += sizeof(float);
				}

				calibration.markerPoses[j].markerId = *(int*)(received.c_str() + i);
				i += sizeof(int);
			}

			m_bStreamOnlyBodies = (received[i] != 0);
			i += 1;

			m_iCompressionLevel = *(int*)(received.c_str() + i);
			i += sizeof(int);
			if (m_iCompressionLevel > 0)
				m_bFrameCompression = true;
			else
				m_bFrameCompression = false;

			//so that we do not lose the next character in the stream
			i--;
		}
		//send stored frame
		else if (received[i] == MSG_REQUEST_STORED_FRAME)
		{
			byteToSend = MSG_STORED_FRAME;
			m_pClientSocket->SendBytes(&byteToSend, 1);

			vector<Point3s> points;
			vector<RGB> colors; 
			bool res = m_framesFileWriterReader.readFrame(points, colors);
			if (res == false)
			{
				int size = -1;
				m_pClientSocket->SendBytes((char*)&size, 4);
			} else
				SendFrame(points, colors, m_vLastFrameBody);
		}
		//send last frame
		else if (received[i] == MSG_REQUEST_LAST_FRAME)
		{

			if (pcheck == 1)
			{
				pcheckcnt++;
			}
			if (pcheckcnt >= 15) //�� 1������ �ӹ�����
			{ 
			pcheck = 0; // veiw ������ �ȵ��� üũ
			pcheckcnt = 0;
			}

			//hunum = 0; // 180414 : Ȯ�� ���

			byteToSend = MSG_LAST_FRAME;
			m_pClientSocket->SendBytes(&byteToSend, 1);

			SendFrame(m_vLastFrameVertices, m_vLastFrameRGB, m_vLastFrameBody);
		}
		//receive calibration data
		else if (received[i] == MSG_RECEIVE_CALIBRATION)
		{
			i++;
			for (int j = 0; j < 3; j++)
			{
				for (int k = 0; k < 3; k++)
				{
					calibration.worldR[j][k] = *(float*)(received.c_str() + i);
					i += sizeof(float);
				}
			}
			for (int j = 0; j < 3; j++)
			{
				calibration.worldT[j] = *(float*)(received.c_str() + i);
				i += sizeof(float);
			}

			//so that we do not lose the next character in the stream
			i--;
		}
		else if (received[i] == MSG_CLEAR_STORED_FRAMES)
		{
			m_framesFileWriterReader.closeFileIfOpened();
		}
		//////// ��ǥ �޾ƿ�

		else if (received[i] == 7)
		{
			i++;

			int np = *(float*)(received.c_str() + i); //�Ӹ�����
			i += sizeof(float);

			float pp[10][4] = { 0 };  // ���ø��� �������� �ʿ� /180414 : �ϴ� �ִ� ����� 10�� 

			/*        => �̰ɷ� �ٲ�� �Ҽ���
			for (int i = 0; i < 10; i++)
			{
				pp[i][0] = 0;
				pp[i][1] = 0;
				pp[i][2] = 0;
			}
			*/



			for (int j = 0; j < np; j++) 
			{
				pp[j][0] = *(float*)(received.c_str() + i);
				i += sizeof(float);
				pp[j][1] = *(float*)(received.c_str() + i);
				i += sizeof(float);
				pp[j][2] = *(float*)(received.c_str() + i);
				i += sizeof(float);
				pp[j][3] = *(float*)(received.c_str() + i);
				i += sizeof(float);

				if (j == 9) // 180414 : �ϴ� �ִ� ����� 10��
				{
					break;  
				}

			}


			//180414 : ��� �� ���� ���� 
			hunum = np;

			if (np > 10)
			{
				hunum = 10;
			}


			// ���� ��� ��°� �ʿ�

			// ����ȯ

			for (int j = 0; j < np; j++)
			{
				if (j < 10)
				{
					Point3f viewpoint(pp[j][0], pp[j][1], pp[j][2]); //���� ������ �̹��� ã��


					viewpoint.X = viewpoint.X * 1000.0f;
					viewpoint.Y = viewpoint.Y * 1000.0f;
					viewpoint.Z = viewpoint.Z * 1000.0f;


					float testpd = 5000;
					float testdb = 0;
					int ttt = 0;

					for (unsigned int i = 0; i < m_vLastFrameVertices.size(); i++)
					{
						testdb = sqrt((m_vLastFrameVertices[i].X - viewpoint.X)*(m_vLastFrameVertices[i].X - viewpoint.X) + (m_vLastFrameVertices[i].Y - viewpoint.Y)*(m_vLastFrameVertices[i].Y - viewpoint.Y) + (m_vLastFrameVertices[i].Z - viewpoint.Z)*(m_vLastFrameVertices[i].Z - viewpoint.Z));

						if (testdb < testpd) //�ּҰ� ã��
						{
							testpd = testdb;
							ttt = i;
						}
					}

					pview[j].X = m_vLastFrameimp[ttt].X; //18.05.07 : pview �ʱ�ȭ �κ� �ʿ�
					pview[j].Y = m_vLastFrameimp[ttt].Y; //�ü� ���Ͱ� ���� �̹����߽���ǥ || Y�� �ø���??  || ����ó�� �ʿ�?
					pview[j].Z = pp[j][3]; // ��� id ����
					// 180410 : pview ���� ����Ʈ �����ϰ� ��� �Ҽ� �ְ� => np ���� ������ �����ؼ� ���

					//////////////////////////////////////////////////////////////////////////////////////////////////////////



					/*
					// �̹��� ���� ���� => 18/04/14 : �� �κ��� ���߿� 1��Ī ���� ������ �� ����� �κ�

					int lx = pview.X - 50;
					int rx = pview.X + 50;

					if (lx < 0) // ����ó��
					{
						lx = 0;
					}
					if (rx > pCapture->nColorFrameWidth) // ����ó��
					{
						rx = pCapture->nColorFrameWidth;
					}


					int ly = pview.Y - 50;
					int ry = pview.Y + 50;

					if (ly < 0) // ����ó��
					{
						ly = 0;
					}
					if (ry > pCapture->nColorFrameHeight) // ����ó��
					{
						ry = pCapture->nColorFrameHeight;
					}


					int xx = rx - lx;
					int yy = ry - ly;
					*/
				}
			}
			//cv::Mat testim(cvSize(pCapture->nColorFrameWidth, pCapture->nColorFrameHeight), CV_8UC3);
			
			//bBoxes = myDetector.detect()


		//	RGBim[i].rgbBlue;

			/*
			int dst1_height, dst1_width;
			uchar*dst1_data; // �����ٸ��� ����?

			dst1_height = viewti->height;
			dst1_width = viewti->widthStep;
			dst1_data = (uchar*)viewti->imageData;


			for (int i = 0; i<dst1_height; i++)
			{
				for (int j = 0; j<dst1_width / 3; j++)
				{
					dst1_data[i*dst1_width + (j * 3)] = RGBim[(i+ly)*pCapture->nColorFrameWidth + j+lx].rgbBlue;
					dst1_data[i*dst1_width + (j * 3) + 1] = RGBim[(i+ly)*pCapture->nColorFrameWidth + j + lx].rgbGreen;
					dst1_data[i*dst1_width + (j * 3) + 2] = RGBim[(i+ly)*pCapture->nColorFrameWidth + j + lx].rgbRed;

				}
			}


			cvNamedWindow("view", 1);
			cvShowImage("view", viewti);
			cvSaveImage("view.jpg", viewti);

			*/

		

			//	pCapture->nColorFrameWidth, pCapture->nColorFrameHeight






			// �̹��� ǥ�� �ʿ�
			pcheck = 1; // �䵥���� ���� üũ

			
			//���� detectp�� ����ȭ �� ���� �����ӿ��� üũ�ϱ�




			// ����

			byteToSend = MSG_LAST_FRAME;
			m_pClientSocket->SendBytes(&byteToSend, 1);

			SendFrame(m_vLastFrameVertices, m_vLastFrameRGB, m_vLastFrameBody);   //������ �θ���
			//so that we do not lose the next character in the stream
		//	i--;
		}


	}

	if (m_bConfirmCaptured)
	{
		byteToSend = MSG_CONFIRM_CAPTURED;
		m_pClientSocket->SendBytes(&byteToSend, 1);
		m_bConfirmCaptured = false;
	}

	if (m_bConfirmCalibrated)
	{
		int size = (9 + 3) * sizeof(float) + sizeof(int) + 1;
		char *buffer = new char[size];
		buffer[0] = MSG_CONFIRM_CALIBRATED;
		int i = 1;

		memcpy(buffer + i, &calibration.iUsedMarkerId, 1 * sizeof(int));
		i += 1 * sizeof(int);
		memcpy(buffer + i, calibration.worldR[0].data(), 3 * sizeof(float));
		i += 3 * sizeof(float);
		memcpy(buffer + i, calibration.worldR[1].data(), 3 * sizeof(float));
		i += 3 * sizeof(float);
		memcpy(buffer + i, calibration.worldR[2].data(), 3 * sizeof(float));
		i += 3 * sizeof(float);
		memcpy(buffer + i, calibration.worldT.data(), 3 * sizeof(float));
		i += 3 * sizeof(float);

		m_pClientSocket->SendBytes(buffer, size);
		m_bConfirmCalibrated = false;
	}
}

void LiveScanClient::SendFrame(vector<Point3s> vertices, vector<RGB> RGB, vector<Body> body)
{
	int size = RGB.size() * (3 + 3 * sizeof(short)) + sizeof(int);

	vector<char> buffer(size + 138);
	char *ptr2 = (char*)vertices.data();
	int pos = 0;

	int nVertices = RGB.size();
	memcpy(buffer.data() + pos, &nVertices, sizeof(nVertices));
	pos += sizeof(nVertices);

	for (unsigned int i = 0; i < RGB.size(); i++)
	{
		buffer[pos++] = RGB[i].rgbRed;
		buffer[pos++] = RGB[i].rgbGreen;
		buffer[pos++] = RGB[i].rgbBlue;

		memcpy(buffer.data() + pos, ptr2, sizeof(short) * 3);
		ptr2 += sizeof(short) * 3;
		pos += sizeof(short) * 3;
	}
	/*
	int nBodies = body.size();
	size += sizeof(nBodies);
	for (int i = 0; i < nBodies; i++)
	{
		size += sizeof(body[i].bTracked);
		int nJoints = body[i].vJoints.size();
		size += sizeof(nJoints);
		size += nJoints * (3 * sizeof(float) + 2 * sizeof(int));
		size += nJoints * 2 * sizeof(float);
	}
	buffer.resize(size);

	memcpy(buffer.data() + pos, &nBodies, sizeof(nBodies));
	pos += sizeof(nBodies);

	for (int i = 0; i < nBodies; i++)
	{
		memcpy(buffer.data() + pos, &body[i].bTracked, sizeof(body[i].bTracked));
		pos += sizeof(body[i].bTracked);

		int nJoints = body[i].vJoints.size();
		memcpy(buffer.data() + pos, &nJoints, sizeof(nJoints));
		pos += sizeof(nJoints);

		for (int j = 0; j < nJoints; j++)
		{
			//Joint
			memcpy(buffer.data() + pos, &body[i].vJoints[j].JointType, sizeof(JointType));
			pos += sizeof(JointType);
			memcpy(buffer.data() + pos, &body[i].vJoints[j].TrackingState, sizeof(TrackingState));
			pos += sizeof(TrackingState);
			//Joint position
			memcpy(buffer.data() + pos, &body[i].vJoints[j].Position.X, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &body[i].vJoints[j].Position.Y, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &body[i].vJoints[j].Position.Z, sizeof(float));
			pos += sizeof(float);

			//JointInColorSpace
			memcpy(buffer.data() + pos, &body[i].vJointsInColorSpace[j].X, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &body[i].vJointsInColorSpace[j].Y, sizeof(float));
			pos += sizeof(float);
		}
	}

	*/


	/////////////////////////// ���//////////////////////////////
	// �� ����-> �� ������ ���� size ���� -> ������? 

	int nFaces = 0;               //Faces.size();     // �󱼰���

	for (int i = 0; i < BODY_COUNT; i++)
	{
		if (facd[i][0] == 1)
		{
			nFaces++;
		}
	}

	/*
		for (int i = 0; i < BODY_COUNT; i++)
	{
		if (pCapture->FaceData[i][0] == 1)
		{
			nFaces++;
		}
	}
	*/



	size += sizeof(nFaces); // ���� ������ Ȯ�� 
	size += nFaces * sizeof(float) * 7; // �� ���� * float * ��ǥ����7��(���߽�, ����ǥ, id)
	buffer.resize(size); // ���� ������ ũ�� ����

	memcpy(buffer.data() + pos, &nFaces, sizeof(nFaces)); //���� �� ���� ���� ������
	pos += sizeof(nFaces);


	for (int i = 0; i < BODY_COUNT; i++)
	{
		if (facd[i][0] == 1) // if (pCapture->FaceData[i][0] == 1)
		{
			// pCapture->FaceData�� Ű��Ʈ �����忡�� ��û ���� ������.... ��� id�� ��� 0���� �ʱ�ȭ��....
			/*
			float headx = pCapture->FaceData[i][6];
			float heady = pCapture->FaceData[i][7];
			float headz = pCapture->FaceData[i][8];

			float nosex = pCapture->FaceData[i][3];
			float nosey = pCapture->FaceData[i][4];
			float nosez = pCapture->FaceData[i][5];

			float pid = pCapture->FaceData[i][9]; //��� id			
			*/

			// int -> float
			float headx = facd[i][6];
			float heady = facd[i][7];
			float headz = facd[i][8];

			float nosex = facd[i][3];
			float nosey = facd[i][4];
			float nosez = facd[i][5];

			float pid = facd[i][9]; //��� id

			headx /= 1000.0f;
			heady /= 1000.0f;
			headz /= 1000.0f;

			nosex /= 1000.0f;
			nosey /= 1000.0f;
			nosez /= 1000.0f;

			if (calibration.bCalibrated)  //���� storeframe ���� ó���ؾ��ϴ� �� �´µ� ����  
			{
				headx += calibration.worldT[0];
				heady += calibration.worldT[1];
				headz += calibration.worldT[2];

				nosex += calibration.worldT[0];
				nosey += calibration.worldT[1];
				nosez += calibration.worldT[2];


				Point3f headpoint(headx, heady, headz);
				Point3f nosepoint(nosex, nosey, nosez);

				headpoint = RotatePoint(headpoint, calibration.worldR); //InverseRotatePoint �� �׽�Ʈ�غ� �ʿ� ����
				nosepoint = RotatePoint(nosepoint, calibration.worldR); //InverseRotatePoint �� �׽�Ʈ�غ� �ʿ� ����

				headx = headpoint.X;
				heady = headpoint.Y;
				headz = headpoint.Z;
				nosex = nosepoint.X;
				nosey = nosepoint.Y;
				nosez = nosepoint.Z;
			}


			memcpy(buffer.data() + pos, &headx, sizeof(float));                    // &face[i].headPosition.X
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &heady, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &headz, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &nosex, sizeof(float));                     // &face[i].nosePosition.X
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &nosey, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &nosez, sizeof(float));
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &pid, sizeof(float));                        // ��� ���̵�
			pos += sizeof(float);
		}
	}


	/////////////////////////////////////////////////////////////
	//18.05.07 ������ �����Ʈ~



	//180531 : �ð� ���� �ӵ� ����
	// �ʱ�ȭ
	int obn;
	obn = 0;

		for (int i = 0; i < 30; i++)
		{
			if (obid[i][0] == 1)
			{
				obn++; // ��� �� ������ ���������� ����Ǵ� obid[i][0] == 0 �̸� break ���ѵ� ���������
			}
		}



		//printf("%d", obn); //18.05.07 ����׿� => �����ϸ� �ߴ��� �������� ���� 



		size += sizeof(obn); // ���� ������ Ȯ�� 
		size += obn * sizeof(float) * 8; // ������Ʈ ���� * float * (������Ʈid 1 + ������Ʈ ��ǥ 3(xyz) + Ű��Ʈ id 1 + �ð� 3(�ú���))
		buffer.resize(size); // ���� ������ ũ�� ����

		memcpy(buffer.data() + pos, &obn, sizeof(obn)); //���� ������Ʈ ���� ���� ������
		pos += sizeof(obn);


		for (int i = 0; i < obn; i++)
		{

			obid[i][2] /= 1000.0f; //mm���� => m����
			obid[i][3] /= 1000.0f;
			obid[i][4] /= 1000.0f;


			//		if (calibration.bCalibrated)  => �̹� cal �Ǿ� �ִٰ� ������ 



			memcpy(buffer.data() + pos, &obid[i][1], sizeof(float));        // object id
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][2], sizeof(float));        // object X��ǥ
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][3], sizeof(float));        // object Y��ǥ
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][4], sizeof(float));        // object Z��ǥ
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][5], sizeof(float));        // Ű��Ʈ id
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][6], sizeof(float));        // ��
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][7], sizeof(float));        // ��
			pos += sizeof(float);
			memcpy(buffer.data() + pos, &obid[i][8], sizeof(float));        // ��
			pos += sizeof(float);

		}

	///////////////////////////////////////////////////////////












	int iCompression = static_cast<int>(m_bFrameCompression);

	if (m_bFrameCompression)
	{
		// *2, because according to zstd documentation, increasing the size of the output buffer above a 
		// bound should speed up the compression.
		int cBuffSize = ZSTD_compressBound(size) * 2;	
		vector<char> compressedBuffer(cBuffSize);
		int cSize = ZSTD_compress(compressedBuffer.data(), cBuffSize, buffer.data(), size, m_iCompressionLevel);
		size = cSize; 
		buffer = compressedBuffer;
	}
	char header[8];
	memcpy(header, (char*)&size, sizeof(size));
	memcpy(header + 4, (char*)&iCompression, sizeof(iCompression));

	m_pClientSocket->SendBytes((char*)&header, sizeof(int) * 2);
	m_pClientSocket->SendBytes(buffer.data(), size);
}

void LiveScanClient::StoreFrame(Point3f *vertices, Point2f *mapping, RGB *color, vector<Body> &bodies, BYTE* bodyIndex)
{
	std::vector<Point3f> goodVertices;
	std::vector<RGB> goodColorPoints;
	std::vector<Point2i> goodimpo; // �̹��� ��ǥ �����

	unsigned int nVertices = pCapture->nDepthFrameWidth * pCapture->nDepthFrameHeight;

	for (unsigned int vertexIndex = 0; vertexIndex < nVertices; vertexIndex++)
	{
		if (m_bStreamOnlyBodies && bodyIndex[vertexIndex] >= bodies.size())
			continue;

		if (vertices[vertexIndex].Z >= 0 && mapping[vertexIndex].Y >= 0 && mapping[vertexIndex].Y < pCapture->nColorFrameHeight)
		{
			Point2i impo;
			impo.X = mapping[vertexIndex].X;
			impo.Y = mapping[vertexIndex].Y; //�̹��� ��ǥ ����

			Point3f temp = vertices[vertexIndex];
			RGB tempColor = color[(int)mapping[vertexIndex].X + (int)mapping[vertexIndex].Y * pCapture->nColorFrameWidth];
			if (calibration.bCalibrated)
			{
				temp.X += calibration.worldT[0];
				temp.Y += calibration.worldT[1];
				temp.Z += calibration.worldT[2];
				temp = RotatePoint(temp, calibration.worldR);

				if (temp.X < m_vBounds[0] || temp.X > m_vBounds[3]
					|| temp.Y < m_vBounds[1] || temp.Y > m_vBounds[4]
					|| temp.Z < m_vBounds[2] || temp.Z > m_vBounds[5])
					continue;
			}

			goodimpo.push_back(impo); //�̹��� ��ǥ ����
			goodVertices.push_back(temp);
			goodColorPoints.push_back(tempColor);
		}
	}

	vector<Body> tempBodies = bodies;

	for (unsigned int i = 0; i < tempBodies.size(); i++)
	{
		for (unsigned int j = 0; j < tempBodies[i].vJoints.size(); j++)
		{
			if (calibration.bCalibrated)
			{
				tempBodies[i].vJoints[j].Position.X += calibration.worldT[0];
				tempBodies[i].vJoints[j].Position.Y += calibration.worldT[1];
				tempBodies[i].vJoints[j].Position.Z += calibration.worldT[2];

				Point3f tempPoint(tempBodies[i].vJoints[j].Position.X, tempBodies[i].vJoints[j].Position.Y, tempBodies[i].vJoints[j].Position.Z);

				tempPoint = RotatePoint(tempPoint, calibration.worldR);

				tempBodies[i].vJoints[j].Position.X = tempPoint.X;
				tempBodies[i].vJoints[j].Position.Y = tempPoint.Y;
				tempBodies[i].vJoints[j].Position.Z = tempPoint.Z;
			}
		}
	}

	if (m_bFilter)
		filtert(goodVertices, goodColorPoints, goodimpo, m_nFilterNeighbors, m_fFilterThreshold);
	// filter(goodVertices, goodColorPoints, m_nFilterNeighbors, m_fFilterThreshold); //����

	vector<Point3s> goodVerticesShort(goodVertices.size());

	for (unsigned int i = 0; i < goodVertices.size(); i++)
	{
		goodVerticesShort[i] = goodVertices[i];
	}

	m_vLastFrameBody = tempBodies;
	m_vLastFrameVertices = goodVerticesShort;
	m_vLastFrameRGB = goodColorPoints;
	m_vLastFrameimp = goodimpo; //�̹��� ����Ʈ����
}

void LiveScanClient::ShowFPS()
{
	if (m_hWnd)
	{
		double fps = 0.0;

		LARGE_INTEGER qpcNow = { 0 };
		if (m_fFreq)
		{
			if (QueryPerformanceCounter(&qpcNow))
			{
				if (m_nLastCounter)
				{
					m_nFramesSinceUpdate++;
					fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
				}
			}
		}

		WCHAR szStatusMessage[64];
		StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f", fps);

		if (SetStatusMessage(szStatusMessage, 1000, false))
		{
			m_nLastCounter = qpcNow.QuadPart;
			m_nFramesSinceUpdate = 0;
		}
	}
}

void LiveScanClient::ReadIPFromFile()
{
	ifstream file;
	file.open("lastIP.txt");
	if (file.is_open())
	{
		char lastUsedIPAddress[20];
		file.getline(lastUsedIPAddress, 20);
		file.close();
		SetDlgItemTextA(m_hWnd, IDC_IP, lastUsedIPAddress);
	}
}

void LiveScanClient::WriteIPToFile()
{
	ofstream file;
	file.open("lastIP.txt");
	char lastUsedIPAddress[20];
	GetDlgItemTextA(m_hWnd, IDC_IP, lastUsedIPAddress, 20);
	file << lastUsedIPAddress;
	file.close();
}
