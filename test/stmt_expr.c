#include "test/something.c"

int main()
{
	int b = 0;
	int a = ({
		b = three();
		3;
	});
	return a == b;
}
