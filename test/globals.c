int a;

void reset_a(void)
{
	int olda = a;
	a = 0;
	return;
}

int increment_a(void)
{
	int olda = a;
	a = a + 1;
	return olda;
}

int decrement_a(void)
{
	int olda = a;
	a = a - 1;
	return olda;
}

int main()
{
	reset_a();
	int x = increment_a(); // x=0
	int y = increment_a(); // y=1
	int z = decrement_a(); // z=2, a=1
	return (x + y + z + a); // should be 4
}
