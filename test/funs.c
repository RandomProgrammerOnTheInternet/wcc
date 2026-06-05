int main()
{
	return doubleit(2) + negative_short(1);
}

long doubleit(long x)
{
	return x * 2;
}

long negative_short(long x)
{
	short y = x;
	y = -y;
	return y;
}
