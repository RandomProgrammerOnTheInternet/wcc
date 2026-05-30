int triangle(int n)
{
	if(n == 0) {
		return 0;
	}
	return n + triangle(n - 1);
}

int main()
{
	return triangle(50);
}
