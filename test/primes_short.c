/* prime number generator, returns maximum prime number in [0, 1000] */
// comment test 5000
short primes()
{
	short *array = malloc(2 * 1000);
	short max = 0;
	short i = 0;
	short iter = 0;

	for(i = 2; i < 1000; i = i + 1) {
		iter = i;
		while(iter < 1000) {
			iter = iter + i;
			array[iter] = 1;
		}
	}

	for(i = 0; i < 1000; i = i + 1) {
		if(array[i] == 0) {
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
