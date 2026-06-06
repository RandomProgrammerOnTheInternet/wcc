int main()
{
	int max_prime = 1;

	for(int i = 2; i < 1000; i = i + 1) {
		int is_prime = 1;
		for(int j = 2; j < i - 1; j = j + 1) {
			if((i % j) == 0) {
				is_prime = 0;
			}
		}

		if(is_prime) {
			max_prime = i;
		}
	}

	print_num(max_prime);

	return max_prime;
}
