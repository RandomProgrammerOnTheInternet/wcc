int main()
{
	int foo;
	_Alignas(16) short bar[5];
	return sizeof foo + (_Alignof(bar) * sizeof(bar));
}
