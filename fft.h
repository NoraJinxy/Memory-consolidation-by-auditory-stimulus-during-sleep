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
#define N  32768       //采样点数  10s-32768， 2s-8192 （必须为2的n次方，否则功率计算出错）

typedef struct        //定义结构体
{
	double real;/*实部*/
	double img; /*虚部*/
}complex;



void add(complex a, complex b, complex *c);
void sub(complex a, complex b, complex *c);      //  复数减运算
void mul(complex a, complex b, complex *c);      //  复数乘运算
void divi(complex a, complex b, complex *c);     // 复数除运算
void initW(int size);
void changex(int size);
void fftx();
void output();
void save();

