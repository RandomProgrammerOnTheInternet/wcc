long one(void)
{
	return 1;
}

long two(long x)
{
	return 2 * x;
}

long argument_waster(long a, long b, long c, long d, long e, long f, long g,
					 long h, long i, long j, long k, long l)
{
	return a + b + c + d + e + f + g + h + i + j + k + l;
}

void putchar(int c);

void print_num_helper(int num)
{
	/* https://stackoverflow.com/a/59389473 */

	if(num >= 10) {
		print_num_helper(num / 10);
	}

	putchar((num % 10) + '0');

	return;
}

void print_num(int num)
{
	print_num_helper(num);
	putchar(10);
	return;
}
