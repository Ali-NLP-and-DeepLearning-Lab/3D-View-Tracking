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
#pragma once

#include "resource.h"
#include "ImageRenderer.h"
#include "SocketCS.h"
#include "calibration.h"
#include "utils.h"
#include "KinectCapture.h"
#include "frameFileWriterReader.h"
#include <thread>
#include <mutex>
#include "filter.h"

class LiveScanClient
{
public:
    LiveScanClient();
    ~LiveScanClient();


    static LRESULT CALLBACK MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK        DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    int                     Run(HINSTANCE hInstance, int nCmdShow);

	bool m_bSocketThread;
private:
	Calibration calibration;

	bool m_bCalibrate;
	bool m_bFilter;
	bool m_bStreamOnlyBodies;

	ICapture *pCapture;

	int m_nFilterNeighbors;
	float m_fFilterThreshold;

	bool m_bCaptureFrame;
	bool m_bConnected;
	bool m_bConfirmCaptured;
	bool m_bConfirmCalibrated;
	bool m_bShowDepth;
	bool m_bFrameCompression;
	int m_iCompressionLevel;

	FrameFileWriterReader m_framesFileWriterReader;

	//////////////////////
	Point3f pview[10];      //�� ����Ʈ ���� /180410 : ���� �迭�� �����Ҵ����� �� �ٲ�ߵ� / 180414 : �ϴ� �� local�� �ִ� ������Ʈ ����� 10��
	int hunum = 0;        // 180414 : �����κ��� ���� �ü� ���� ��� ��
	//////////////////////
	float facd[6][10]; // ���� ��� ����
	/////////////////////
	float obid[30][9]; // 18.05.07 : �ִ� 30�� , ������Ʈ ���� , ������Ʈ id, ������Ʈ 3D ��ǥ(x,y,z) , Ű��Ʈid, �ð�(��, ��, ��)
	float viid[30][7]; // 18.05.30 : 2���� ���� ���� �̹��� x��ǥ, �̹��� y��ǥ, �̹��� �ʺ�, �̹��� �ʺ�,������Ʈ ���̵�, ������Ʈ Ȯ��, ������Ʈ �ּ� �� �Ķ���� ����
	/////////////////////
	cv::Mat *frames;
	/////////////////////
	int pcheck = 0;    // �䵥���� �ۼ��� üũ
    /////////////////
	int pcheckcnt = 0;
	/////////////////

	SocketClient *m_pClientSocket;
	std::vector<float> m_vBounds;

	std::vector<Point3s> m_vLastFrameVertices;
	std::vector<RGB> m_vLastFrameRGB;
	std::vector<Body> m_vLastFrameBody;
	std::vector<Point2i> m_vLastFrameimp;

	HWND m_hWnd;
    INT64 m_nLastCounter;
    double m_fFreq;
    INT64 m_nNextStatusTime;
    DWORD m_nFramesSinceUpdate;	

	Point3f* m_pCameraSpaceCoordinates;
	Point2f* m_pColorCoordinatesOfDepth;
	Point2f* m_pDepthCoordinatesOfColor;

    // Direct2D
    ImageRenderer* m_pDrawColor;
    ID2D1Factory* m_pD2DFactory;
	RGB* m_pDepthRGBX;

	void UpdateFrame();
    void ProcessColor(RGB* pBuffer, int nWidth, int nHeight);
	void ProcessDepth(const UINT16* pBuffer, int nHeight, int nWidth);

    bool SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce);

	void HandleSocket();
	void SendFrame(vector<Point3s> vertices, vector<RGB> RGB, vector<Body> body);

	void SocketThreadFunction();
	void StoreFrame(Point3f *vertices, Point2f *mapping, RGB *color, vector<Body> &bodies, BYTE* bodyIndex);
	void ShowFPS();
	void ReadIPFromFile();
	void WriteIPToFile();
};

