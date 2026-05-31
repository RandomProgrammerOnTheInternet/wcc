/* prime number generator, returns maximum prime number in [0, 1000] */
// comment test 5000
int primes()
{
	int *array = malloc(4 * 1000);
	int max = 0;
	int i = 0;
	int iter = 0;

	for(i = 2; i < 1000; i = i + 1) {
		iter = i;
		while(iter < 1000) {
			iter = iter + i;
			*(array + iter) = 1;
		}
	}

	for(i = 0; i < 1000; i = i + 1) {
		if(*(array + i) == 0) {
			if(i > max) {
				max = i;
			}
		}
	}

	print_num(max);
	free(array);
	return max;
}

int main()
{
	return primes();
}
