#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <vector>
#include  <ctime>
#include <iomanip>

using namespace std;

#define PI 3.14159265  
#define N  32768       //��������  10s-32768�� 2s-8192 ������Ϊ2��n�η��������ʼ������

typedef struct        //����ṹ��
{
	double real;/*ʵ��*/
	double img; /*�鲿*/
}complex;



void add(complex a, complex b, complex *c);
void sub(complex a, complex b, complex *c);      //  ����������
void mul(complex a, complex b, complex *c);      //  ����������
void divi(complex a, complex b, complex *c);     // ����������
void initW(int size);
void changex(int size);
void fftx();
void output();
void save();

