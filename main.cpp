#pragma once
#include "CClient.h"
#include "windows.h"

int main(int argc, char** argv)
{
	char filename[100] = "E Sub02 20230422 17 Run02";
	CClient eeg_data(filename);
	Sleep(1000);

	std::cout << "Start Recording..." << std::endl;
	while (!_kbhit())
	{

	}
	eeg_data.is_Connect = false;
	Sleep(2000);


	return 0;
}