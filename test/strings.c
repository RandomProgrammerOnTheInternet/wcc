int main()
{
	char *str = "hello world";
	char *str2 = "hello world!";
	char first_letter = "h"[0];
	char first_letter_lit = 'h';

	if(first_letter != first_letter_lit) {
		return 0;
	}

	for(int i = 0; i < strlen(str); i = i + 1) {
		if(str[i] != str2[i]) {
			return 0;
		}
	}

	return (str[0] == first_letter) && (str[0] == first_letter_lit);
}
