long argument_waster2(long a, long b, long c, long d, long e, long f, long g,
					  long h, long i, long j, long k, long l)
{
	return a + b + c + d + e + f + g + h + i + j + k + l;
}

int main()
{
	long n = 12;
	long sum = argument_waster(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, n);
	long sum2 = argument_waster2(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, n);
	long gauss_swag_route = (n * (n + 1)) / 2;

	return ((sum + sum2) / 2) + gauss_swag_route;
}
