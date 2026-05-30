int main()
{
	long a = 1;
	{
		a = 2;
		{
			long b = 3;
		}
	}
	return (b + a) - 1;
}
