int main()
{
	long a = 0, b = 42;
	do {
		a += 1;
		b += 2;
	} while((a + b) != 123);
	return a + b;
}
