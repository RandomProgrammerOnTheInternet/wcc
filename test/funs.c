int main()
{
	return doubleit(2) + negative_short(1) + boolean() - boolean();
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

_Bool boolean(void)
{
	_Bool y = 3;
	return y;
}
