int process(int *c)
{
	*c = 2;
	return 4;
}

int main()
{
	_Alignas(64) int c = 3;
	_Alignas(16) int d = process(&c);
	return c + d;
}
