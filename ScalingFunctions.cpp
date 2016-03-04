#include "ScalingFunctions.h"

double scale_linear(double x, double min_x, double max_x, double min_y, double max_y)
{
	if(x<min_x) x=min_x;
	if(x>max_x) x=max_x;
	double x_range = max_x-min_x;
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
	float y_range = max_y-min_y;
	float r = (x-min_x)/x_range;
	float y = min_y + r*y_range;
	return y;
}
