/* prime number generator, returns maximum prime number in [0, 1000] */
// comment test 5000

#include "test/one.c"
int primes()
{
	short array[1000];
	short max = 0;
	short iter = 0;

	for(int i = 0; i < 1000; ++i) {
		array[i] ^= array[i];
	}
	puts("xor ok");

	for(int i = 2; i < 1000; i++) {
		iter = i << 1;
		while(iter < 1000) {
			array[iter] |= 1;
			iter += i;
		}
	}
	puts("sieve ok");

	for(int i = 0; i < 1000; ++i) {
		if(!array[i] && i > max) {
			max = i;
		}
	}
	puts("chk ok");

	print_num(max);
	return max;
}

int main()
{
	return primes();
}
