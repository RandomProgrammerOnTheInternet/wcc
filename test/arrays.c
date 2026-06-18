
int main()
{
	int array1[3][3];

	array1[0][0] = 0;
	array1[0][1] = 1;
	array1[0][2] = 2;
	array1[1][0] = 1;
	array1[1][1] = 2;
	array1[1][2] = 3;
	array1[2][0] = 2;
	array1[2][1] = 3;
	array1[2][2] = 4;

	for(int i = 0; i < 3; i++) {
		for(int j = 0; j < 3; j++) {
			int sum = array1[i][j];
			if(sum != (i + j)) {
				return 0; /* not ok */
			}
		}
	}

	return 1; /* ok */
}
