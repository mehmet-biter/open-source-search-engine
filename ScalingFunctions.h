#ifndef GB_SCALINGFUNCTIONS_H
#define GB_SCALINGFUNCTIONS_H

//simple y = f(x) functions, used for a variety of purposes.

//limit x into range [min_x..max_x], then scale linearly into [min_y..max_y]
double scale_linear(double x, double min_x, double max_x, double min_y, double max_y);
float scale_linear(float x, float min_x, float max_x, float min_y, float max_y);


//limit x into range [min_x..max_x], then scale using a quadratic expression  into [min_y..max_y]
double scale_quadratic(double x, double min_x, double max_x, double min_y, double max_y);


//limit x into range [min_x..max_x], then scale logarithmically into [min_y..max_y]
double scale_logarithmically(double x, double min_x, double max_x, double min_y, double max_y);


#endif // GB_SCALINGFUNCTIONS_H
