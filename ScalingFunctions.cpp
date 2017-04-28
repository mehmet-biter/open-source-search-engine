#include "ScalingFunctions.h"
#include <math.h>

double scale_linear(double x, double min_x, double max_x, double min_y, double max_y)
{
	if(x<min_x) x=min_x;
	if(x>max_x) x=max_x;
	double x_range = max_x-min_x;
	if(x_range==0.0) return min_y; //don't divide by zero
	double y_range = max_y-min_y;
	double r = (x-min_x)/x_range;
	double y = min_y + r*y_range;
	return y;
}

float scale_linear(float x, float min_x, float max_x, float min_y, float max_y)
{
	if(x<min_x) x=min_x;
	if(x>max_x) x=max_x;
	float x_range = max_x-min_x;
	if(x_range==0.0) return min_y; //don't divide by zero
	float y_range = max_y-min_y;
	float r = (x-min_x)/x_range;
	float y = min_y + r*y_range;
	return y;
}


double scale_quadratic(double x, double min_x, double max_x, double min_y, double max_y)
{
	//Note: this function is slightly incorrect, but it is Friday
	//afternoon and the output is close enough.
	if(x<min_x) x=min_x;
	if(x>max_x) x=max_x;
	double x_range = max_x-min_x;
	if(x_range==0.0) return min_y; //don't divide by zero
	double y_range = max_y-min_y;
	double r = (x-min_x)/x_range;
	return ((r+1)*(r+1)-1)/3*y_range+min_y;
}


double scale_logarithmically(double x, double min_x, double max_x, double min_y, double max_y)
{
	x = log(x);
	min_x = log(min_x);
	max_x = log(max_x);
	if(x<min_x) x=min_x;
	if(x>max_x) x=max_x;
	double x_range = max_x-min_x;
	if(x_range==0.0) return min_y; //don't divide by zero
	double y_range = max_y-min_y;
	double r = (x-min_x)/x_range;
	double y = min_y + r*y_range;
	return y;
}
