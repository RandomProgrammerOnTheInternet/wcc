/* prime number generator, returns maximum prime number in [0, 1000] */
// comment test 5000
int primes()
{
	short array[1000];
	short max = 0;
	short iter = 0;

	for(int i = 0; i < 1000; i = i + 1) {
		array[i] = array[i] ^ array[i];
	}

	for(int i = 2; i < 1000; i = i + 1) {
		iter = i << 1;
		while(iter < 1000) {
			array[iter] = array[iter] | 1;
			iter = iter + i;
		}
	}

	for(int i = 0; i < 1000; i = i + 1) {
		if(!array[i] && i > max) {
			max = i;
		}
	}

	print_num(max);
	return max;
}

int main()
{
	return primes();
}
