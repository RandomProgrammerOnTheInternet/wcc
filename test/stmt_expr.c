int main()
{
	int b = 0;
	int a = ({
		b = 3;
		3;
	});
	return a == b;
}
