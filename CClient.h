#include "Head.h"
#include"fft.h"

complex x[N * 2], * W;

static DWORD WINAPI ReadThreadEntry(PVOID arg);
static DWORD WINAPI WriteThreadEntry(PVOID arg);

class CClient
{
public:
	short	data_raw[DATA_LENGTH][DATA_CHANNEL];
	double	data_filtered1[DATA_LENGTH][DATA_CHANNEL];
	double	data_filtered[DATA_LENGTH][DATA_CHANNEL];
	double	data_ref[DATA_LENGTH][1];
	double	fs = 5000;
	double	df = fs / N;
	double	power[N * 2] = { 0 };
	unsigned long data_index;
	unsigned long random_index;
	unsigned long file_index;
	int		filter_order;
	double	filter_a_stop[50];
	double	filter_b_stop[50];
	double	filter_a_pass[50];
	double	filter_b_pass[50];
	double	T_Play_Sound1 = 3 * 60 * 1000;
	double	T_Slience1 = 4 * 60 * 1000;
	double	T_Play_Sound2 = 7 * 60 * 1000;
	double	T_Slience2 = 8 * 60 * 1000;
	int		stimu1 = 100;
	int		stimu3 = 300;
	int		give_stimu_num;
	int		stop_stimu_num;

	FILE* fpHeader;
	FILE* fpData;
	FILE* fpMarkers;

	int					Midi_note;
	HANDLE				DeviceAmp;							// Amplifier device
	HANDLE				Read_Thread;						//收包线程句柄
	HANDLE				Write_Thread;						//收包线程句柄
	char				cHeaderFile[256], cDataFile[256], cMarkerFile[256];
	char				filename[256];
	bool				UsbDevice;
	AmpTypes			amplifiers[4];
	BA_SETUP			Setup;
	bool				is_Connect;
	bool				if_Stimu;
	bool				if_Calculate;

	CClient(char* src)
	{
		memset(data_raw, 0, DATA_LENGTH * DATA_CHANNEL * sizeof(short));
		memset(data_filtered1, 0, DATA_LENGTH * DATA_CHANNEL * sizeof(double));
		memset(data_filtered, 0, DATA_LENGTH * DATA_CHANNEL * sizeof(double));
		memset(data_ref, 0, DATA_LENGTH * 1 * sizeof(double));
		memset(filter_a_stop, 0, 50 * sizeof(double));
		memset(filter_b_stop, 0, 50 * sizeof(double));
		memset(filter_a_pass, 0, 50 * sizeof(double));
		memset(filter_b_pass, 0, 50 * sizeof(double));
		data_index = 0;
		file_index = 0;

		const char* c_write_file1 = "filter_coef.bin";
		FILE* fp = fopen(c_write_file1, "rb");
		if (fp == NULL) {
			cout << "Open file recfile.\n";
		}
		else
		{
			fread(&filter_order, sizeof(int), 1, fp);
			for (int i = 0; i < filter_order; i++)
				fread(&filter_a_stop[i], sizeof(double), 1, fp);
			for (int i = 0; i < filter_order; i++)
				fread(&filter_b_stop[i], sizeof(double), 1, fp);
			for (int i = 0; i < filter_order; i++)
				fread(&filter_a_pass[i], sizeof(double), 1, fp);
			for (int i = 0; i < filter_order; i++)
				fread(&filter_b_pass[i], sizeof(double), 1, fp);
			fclose(fp);
		}

		strcpy(filename, src);


		DeviceAmp = INVALID_HANDLE_VALUE;
		UsbDevice = false;
		fpHeader = NULL;
		fpData = NULL;
		fpMarkers = NULL;
		is_Connect = false;
		if_Stimu = false;
		if_Calculate = false;

		if (!OpenDevice())
		{
			printf("No BrainAmp USB adapter and no ISA/PCI adapter found!\n");
			while (!_kbhit())
			{
				Sleep(1);
			}
			return;
		}

		ZeroMemory(&Setup, sizeof(Setup));
		Setup.nChannels = DATA_CHANNEL - 1;
		Setup.nHoldValue = 0x0;		// Value without trigger
		Setup.nPoints = 1;//4 * 5;		// 5 kHz = 5 points per ms -> 40 ms data block
		for (int i = 0; i < Setup.nChannels; i++)
			Setup.nChannelList[i] = i;

		DWORD dwBytesReturned = 0;
		if (!DeviceIoControl(DeviceAmp, IOCTL_BA_SETUP, &Setup,
			sizeof(Setup), NULL, 0, &dwBytesReturned, NULL))
		{
			printf("Setup failed, error code: %u\n", ::GetLastError());
		}
		unsigned short pullup = 0;
		if (!DeviceIoControl(DeviceAmp, IOCTL_BA_DIGITALINPUT_PULL_UP, &pullup,
			sizeof(pullup), NULL, 0, &dwBytesReturned, NULL))
		{
			printf("Can't set pull up/down resistors, error code: %u\n",
				::GetLastError());
		}

		// Make sure that amps exist, otherwise a long timeout will occur.
		int nHighestChannel = 0;
		for (int i = 0; i < Setup.nChannels; i++)
		{
			nHighestChannel = max(Setup.nChannelList[i], nHighestChannel);
		}
		int nRequiredAmps = (nHighestChannel + 1) / 32;
		int nAmps = FindAmplifiers();
		if (nAmps < nRequiredAmps)
		{
			printf("Required Amplifiers: %d, Connected Amplifiers: %d\n",
				nRequiredAmps, nAmps);
			while (!_kbhit())
			{
				Sleep(1);
			}
			return;
		}
		// Start acquisition
		long acquisitionType = 1;
		if (!DeviceIoControl(DeviceAmp, IOCTL_BA_START, &acquisitionType,
			sizeof(acquisitionType), NULL, 0, &dwBytesReturned, NULL))
		{
			printf("Start failed, error code: %u\n", ::GetLastError());
		}
		is_Connect = true;
		//Read_Thread=CreateThread(NULL,0,ReadThreadEntry,(PVOID)this,0,NULL);
		Read_Thread = CreateThread(NULL, 0, ReadThreadEntry, (PVOID)this, CREATE_SUSPENDED, NULL);
		::SetThreadPriority(Read_Thread, THREAD_PRIORITY_TIME_CRITICAL);
		::ResumeThread(Read_Thread);
		Write_Thread = CreateThread(NULL, 0, WriteThreadEntry, (PVOID)this, 0, NULL);
	}

	void ReadData()
	{
		vector<short>pnData((Setup.nChannels + 1) * Setup.nPoints);
		short* pSrc = &pnData[0];
		int nTransferSize = (int)pnData.size() * sizeof(short);
		DWORD dwBytesReturned;
		ReadFile(DeviceAmp, &pnData[0], nTransferSize, &dwBytesReturned, NULL);
		while (dwBytesReturned)
		{
			ReadFile(DeviceAmp, &pnData[0], nTransferSize, &dwBytesReturned, NULL);
		}
		cout << "ReadFile\n";

		ofstream DeltaPower;
		DeltaPower.open("Sub02_DeltaPower_20230422_run02.dat"); // opens the file
		if (!DeltaPower)
		{ // file couldn't be opened
			cerr << "Error: file could not be opened" << endl;
			exit(1);
		}

		double	dTotalTime;
		double	t1 = 0;
		int		stimu_num = 0;
		bool	round_start = false;
		bool	round_stimu_start = true;
		_int64	start_stimu;
		_int64	start_round_time;
		_int64	start_calculation_time;
		_int64	end_calculation_time;
		LARGE_INTEGER  litmp, start, end;
		QueryPerformanceFrequency(&litmp);
		QueryPerformanceCounter(&start);
		QueryPerformanceCounter(&end);
		start_calculation_time = end.QuadPart;
		end_calculation_time = end.QuadPart;
		start_round_time = end.QuadPart;
		start_stimu = end.QuadPart;

		dTotalTime = (double)(end.QuadPart - start.QuadPart) * 1000 / (double)litmp.QuadPart;

		while (is_Connect)
		{
			while (dTotalTime - data_index * 1.0 / 5 < 60)
			{
				QueryPerformanceCounter(&end);
				dTotalTime = (double)(end.QuadPart - start.QuadPart) * 1000 / (double)litmp.QuadPart;
			}
			int nTemp = 0;
			if (!DeviceIoControl(DeviceAmp, IOCTL_BA_ERROR_STATE, NULL, 0,
				&nTemp, sizeof(nTemp), &dwBytesReturned, NULL))
			{
				printf("Acquisition Error, GetLastError(): %d\n", ::GetLastError());
				while (!_kbhit())
				{
					Sleep(1);
				}
				return;
			}
			if (nTemp != 0)
			{
				printf("Acquisition Error %d\n", nTemp);
				while (!_kbhit())
				{
					Sleep(1);
				}
				return;
			}
			if (!ReadFile(DeviceAmp, &pnData[0], nTransferSize, &dwBytesReturned, NULL))
			{
				printf("Acquisition Error, GetLastError(): %d\n", ::GetLastError());
				while (!_kbhit())
				{
					Sleep(1);
				}
				return;
			}
			if (!dwBytesReturned)
			{
				Sleep(1);
				continue;
			}
			pSrc = &pnData[0];
			for (int i = 0; i < Setup.nPoints; i++)
			{
				//get the raw data
				
				for (int n = 0; n < Setup.nChannels + 1; n++)
				{
					data_raw[data_index % DATA_LENGTH][n] = *(pSrc++);
				}
				//data_raw[data_index % DATA_LENGTH][Setup.nChannels] = 0;

				//data filtering
				//norch
				data_filtered1[data_index % DATA_LENGTH][Setup.nChannels] = data_raw[data_index % DATA_LENGTH][Setup.nChannels];
				for (int n = 0; n < Setup.nChannels; n++)
				{
					data_filtered1[data_index % DATA_LENGTH][n] = filter_b_stop[0] * (double)data_raw[data_index % DATA_LENGTH][n];
					for (int j = 1; j < filter_order; j++)
					{
						data_filtered1[data_index % DATA_LENGTH][n] += filter_b_stop[j] * (double)data_raw[(data_index - j + DATA_LENGTH) % DATA_LENGTH][n];
						data_filtered1[data_index % DATA_LENGTH][n] -= filter_a_stop[j] * (double)data_filtered1[(data_index - j + DATA_LENGTH) % DATA_LENGTH][n];
					}
				}
				//bandpass
				data_filtered[data_index % DATA_LENGTH][Setup.nChannels] = data_filtered1[data_index % DATA_LENGTH][Setup.nChannels];
				for (int n = 0; n < Setup.nChannels; n++)
				{
					data_filtered[data_index % DATA_LENGTH][n] = filter_b_pass[0] * (double)data_filtered1[data_index % DATA_LENGTH][n];
					for (int j = 1; j < filter_order; j++)
					{
						data_filtered[data_index % DATA_LENGTH][n] += filter_b_pass[j] * (double)data_filtered1[(data_index - j + DATA_LENGTH) % DATA_LENGTH][n];
						data_filtered[data_index % DATA_LENGTH][n] -= filter_a_pass[j] * (double)data_filtered[(data_index - j + DATA_LENGTH) % DATA_LENGTH][n];
					}
				}
				//rereference to TP9&TP10 !!!!!!!!!!!!!!!!!!!!!
				data_ref[data_index % DATA_LENGTH][0] = data_raw[data_index % DATA_LENGTH][CHANNEL_INDEX] -
					(data_raw[data_index % DATA_LENGTH][28] + data_raw[data_index % DATA_LENGTH][29]) / 2;

				QueryPerformanceCounter(&end);
				end_calculation_time = end.QuadPart;
				dTotalTime = (double)(end_calculation_time - start_calculation_time) * 1000 / (double)litmp.QuadPart;
				
				if (data_index > N)
				{	//计算0-4Hz的平均能量,10s算一次
					if (dTotalTime >= 10*1000)
					{
						std::cout << "Calculation Time:  " << dTotalTime << std::endl;
						QueryPerformanceCounter(&end);
						start_calculation_time = end.QuadPart;
						end_calculation_time = end.QuadPart;

						cout << "输出信号：";
						cout << endl;
						for (int i = 0; i < N; i++)
						{
							//c++计算中的信号的幅度是MATLAB记录到的10倍
							x[i].real = data_ref[(data_index - N + i) % DATA_LENGTH][0]/10;
							x[i].img = 0;
							//printf("%0.4f\t", x[i].real);
						}
						initW(N);
						fftx();
						
						for (i = 0; i < N; i++)
						{
							power[i] = log(sqrt(x[i].real * x[i].real + x[i].img * x[i].img));
						}
						double	delta_power = 0;
						for (i = 0; i <= 27; i++)
						{
							delta_power = delta_power + power[i];
						}

						double mean_power = delta_power / (28);
						std::cout << "Mean Power:  " << mean_power << "    data_index" << data_index << std::endl;
						DeltaPower << mean_power << '\t' << data_index << endl;
						if (mean_power > 10.5 && !if_Stimu)
						{
							give_stimu_num++; stop_stimu_num = 0;
						}
						if (mean_power < 10.5) 
						{
							give_stimu_num = 0; stop_stimu_num++;
						}
					}
					//连续 3min（180s）delta平均能量高于阈值，则判定进入N3期
					if (give_stimu_num >= 18 && !if_Stimu)
					{
						if_Stimu = true;
						if (round_stimu_start) { round_start = true; }
					}
					if (stop_stimu_num >= 6 && if_Stimu)
					{
						if_Stimu = false;
					}
					//进入N3期，开始给刺激
					//初始化大循环时间
					if (if_Stimu && round_start)
					{
						QueryPerformanceCounter(&end);
						start_round_time = end.QuadPart;
					}
					//计算大循环时间（3min声音1 + 1min静息 + 3min声音3 + 1min静息）
					QueryPerformanceCounter(&end);
					double roundTime = (double)(end.QuadPart - start_round_time) * 1000 / (double)litmp.QuadPart;
					double stimuTime = (double)(end.QuadPart - start_stimu) * 1000 / (double)litmp.QuadPart;
					//3min声音1刺激
					if (if_Stimu && roundTime <= T_Play_Sound1 && stimu_num == 0)
					{
						QueryPerformanceCounter(&end);
						start_stimu = end.QuadPart;
						double stimuTime = (double)(end.QuadPart - start_stimu) * 1000 / (double)litmp.QuadPart;

						data_raw[data_index % DATA_LENGTH][Setup.nChannels] = stimu1;
						PlaySound(MAKEINTRESOURCE(IDR_WAVE1), NULL, SND_RESOURCE | SND_ASYNC);
						stimu_num++;
						round_start = false;
						round_stimu_start = false;
					}
					if (if_Stimu && stimuTime >= (5 * 1000 - t1) && roundTime <= T_Play_Sound1)
					{
						data_raw[data_index % DATA_LENGTH][Setup.nChannels] = stimu1;
						PlaySound(MAKEINTRESOURCE(IDR_WAVE1), NULL, SND_RESOURCE | SND_ASYNC);
						t1 = roundTime - 5 * 1000 * stimu_num;
						
						QueryPerformanceCounter(&end);
						start_stimu = end.QuadPart;
						stimu_num++;
					}
					//1min静息
					if (if_Stimu && roundTime > T_Play_Sound1 && roundTime < T_Slience1)
					{
						t1 = 0; stimu_num = 0;
						QueryPerformanceCounter(&end);
						start_stimu = end.QuadPart;
					}
					//3min声音3刺激
					if (if_Stimu && roundTime >= T_Slience1 && roundTime < T_Play_Sound2 && stimu_num == 0)
					{
						QueryPerformanceCounter(&end);
						start_stimu = end.QuadPart;
						double stimuTime = (double)(end.QuadPart - start_stimu) * 1000 / (double)litmp.QuadPart;

						data_raw[data_index % DATA_LENGTH][Setup.nChannels] = stimu3;
						PlaySound(MAKEINTRESOURCE(IDR_WAVE2), NULL, SND_RESOURCE | SND_ASYNC);
						stimu_num++;
					}
					if (if_Stimu && stimuTime >= (5 * 1000 - t1) && roundTime > T_Slience1 && roundTime < T_Play_Sound2)
					{
						data_raw[data_index % DATA_LENGTH][Setup.nChannels] = stimu3;
						PlaySound(MAKEINTRESOURCE(IDR_WAVE2), NULL, SND_RESOURCE | SND_ASYNC);
						t1 = roundTime - T_Slience1 - 5 * 1000 * stimu_num;

						QueryPerformanceCounter(&end);
						start_stimu = end.QuadPart;
						stimu_num++;
					}
					//1min静息
					if (if_Stimu && roundTime > T_Play_Sound2) { t1 = 0;  stimu_num = 0; }
					
					if (roundTime > T_Slience2)
					{
						QueryPerformanceCounter(&end);
						start_round_time = end.QuadPart;
						start_stimu = end.QuadPart;
						round_stimu_start = true;
					}
				}
				data_index++;
			}
			QueryPerformanceCounter(&end);
			dTotalTime = (double)(end.QuadPart - start.QuadPart) * 1000 / (double)litmp.QuadPart;
		}
		if (!DeviceIoControl(DeviceAmp, IOCTL_BA_STOP, NULL, 0, NULL, 0,
			&dwBytesReturned, NULL))
		{
			printf("Stop failed, error code: %u\n", ::GetLastError());
		}
		if (DeviceAmp != INVALID_HANDLE_VALUE)
		{
			CloseHandle(DeviceAmp);
		}
	}
	void WriteData()
	{
		if (!OpenNewFiles() || !WriteHeaderFile())
		{
			printf("Can't create/access data file(s)!\n");
			while (!_kbhit())
			{
				Sleep(1);
			}
			return;
		}
		unsigned short nLastMarkerValue = 0;
		unsigned int nMarkerNumber = 0;
		while (is_Connect || file_index < data_index)
		{
			if (file_index < data_index)
			{
				for (int n = 0; n < Setup.nChannels; n++)
				{
					if (fwrite(&data_raw[file_index % DATA_LENGTH][n], sizeof(short), 1, fpData) != 1)
					{
						while (!_kbhit())
						{
							Sleep(1);
						}
						return;
					}
				}
				unsigned short nMarkerValue = data_raw[file_index % DATA_LENGTH][Setup.nChannels];
				nMarkerValue ^= Setup.nHoldValue;
				if (nMarkerValue != nLastMarkerValue)
				{
					nLastMarkerValue = nMarkerValue;
					if (nMarkerValue != 0)	// New marker?
					{
						if (fprintf(fpMarkers, "Mk%u=Marker,M%lu,%u,1,0\n",
							++nMarkerNumber, nMarkerValue, file_index + 1) <= 0)
						{
							printf("Write error in marker file!\n");
							while (!_kbhit())
							{
								Sleep(1);
							}
							return;
						}
						//cout <<nMarkerValue << '\n';
					}
				}
				file_index++;
			}
			else
			{
				Sleep(10);
			}
		}
		CloseFiles();
	}
	bool OpenDevice()
	{
		DWORD dwFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH;
		// First try USB box
		DeviceAmp = CreateFile(DEVICE_USB, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, dwFlags, NULL);
		if (DeviceAmp != INVALID_HANDLE_VALUE)
		{
			UsbDevice = true;
		}
		else
		{
			// USB box not found, try PCI host adapter
			UsbDevice = false;;
			DeviceAmp = CreateFile(DEVICE_PCI, GENERIC_READ | GENERIC_WRITE, 0, NULL,
				OPEN_EXISTING, dwFlags, NULL);
		}

		return DeviceAmp != INVALID_HANDLE_VALUE;
	}
	int FindAmplifiers()
	{
		USHORT amps[4];
		DWORD dwBytesReturned;

		DeviceIoControl(DeviceAmp, IOCTL_BA_AMPLIFIER_TYPE, NULL, 0, amps,
			sizeof(amps), &dwBytesReturned, NULL);

		int nAmps = 4;
		for (int i = 0; i < 4; i++)
		{
			amplifiers[i] = (AmpTypes)amps[i];
			if (amplifiers[i] == None && i < nAmps)
			{
				nAmps = i;
			}
		}
		return nAmps;
	}
	bool OpenNewFiles()
	{
		CloseFiles();
		char str_temp[256];
		sprintf(str_temp, "data\\%s", filename);
		sprintf(cHeaderFile, "%s.vhdr", str_temp);
		sprintf(cDataFile, "%s.eeg", str_temp);
		sprintf(cMarkerFile, "%s.vmrk", str_temp);

		printf("Writing to %s...\n", cDataFile);
		// Open files
		fpHeader = fopen(cHeaderFile, "wt");	// Text file
		if (!fpHeader) return false;
		fpData = fopen(cDataFile, "wb");		// Binary file
		if (!fpData) { CloseFiles(); return false; }
		fpMarkers = fopen(cMarkerFile, "wt");	// Text file
		if (!fpMarkers) { CloseFiles(); return false; }
		// Write headlines into marker file.
		if (fprintf(fpMarkers,
			"Brain Vision Data Exchange Marker File, Version 1.0\n\n"
			"[Common Infos]\nDataFile=%s.eeg\n\n\n[Marker Infos]\n",
			filename) <= 0)
		{
			return false;
		}
		return true;
	}
	void CloseFiles()
	{
		if (fpHeader) { fclose(fpHeader); fpHeader = NULL; }
		if (fpData) { fclose(fpData); fpData = NULL; }
		if (fpMarkers) { fclose(fpMarkers);	fpMarkers = NULL; }
	}
	bool WriteHeaderFile()
	{
		int nChannels = Setup.nChannels;

		// Common stuff, number of channels, sampling interval
		if (fprintf(fpHeader, "Brain Vision Data Exchange Header File Version 1.0\n"
			"; Created by the BrainAmpControl example\n\n"
			"[Common Infos]\nDataFile=%s.eeg\nMarkerFile=%s.vmrk\nDataFormat=BINARY\n"
			"DataOrientation=MULTIPLEXED\nNumberOfChannels=%u\nSamplingInterval=200\n\n"
			"[Binary Infos]\nBinaryFormat=INT_16\n\n[Channel Infos]\n"
			"; Each entry: Ch<Channel number>=<Name>,<Reference channel name>, \n"
			"; <Resolution in \"Unit\">,<Unit>, Future extensions..\n"
			"; Fields are delimited by commas, some fields might be omitted (empty).\n"
			"; Commas in channel names are coded as \"\\1\". \n",
			filename, filename, Setup.nChannels) <= 0) return false;

		// Channel info
		for (int i = 0; i < Setup.nChannels; i++)
		{
			// Resolution
			double dResolution[] = { .1, .5, 10., 152.6 };
			if (fprintf(fpHeader, "Ch%u=%u,,%.1f\n",
				i + 1, i + 1, dResolution[0]) <= 0)
			{
				return false;
			}
		}
		fclose(fpHeader);
		fpHeader = NULL;
		return true;
	}
};

static DWORD WINAPI ReadThreadEntry(PVOID arg)
{
	srand((unsigned)time(NULL));
	((CClient*)arg)->ReadData();
	cout << "ReadThread End\n";
	return 0;
}
static DWORD WINAPI WriteThreadEntry(PVOID arg)
{
	((CClient*)arg)->WriteData();
	cout << "WriteThread End\n";
	return 0;
}