/* prime number generator, returns maximum prime number in [0, 1000] */
// comment test 5000
int primes()
{
	int max = 0;
	int i = 0;
	int iter = 0;
	int *array = malloc(1000 * sizeof i);

	for(i = 0; i < 1000; i += 1) {
		*(array + i) = 0;
	}

	for(i = 2; i < 1000; i += 1) {
		iter = 2 * i;
		while(iter < 1000) {
			*(array + iter) = 1;
			iter += i;
		}
	}

	for(i = 0; i < 1000; i += 1) {
		if(!*(array + i)) {
			if(i > max) {
				max = i;
			}
		}
	}

	print_num(max);

	free(array);

	return max;
}

#include "test/one.c"

int main()
{
	return primes();
}
