#include <stdio.h>
#include <string.h>

#include "testing.h"
#include "../base64.h"


void test_base64_char_to_6bit() {
	check_int( base64_char_to_6bit('A'), 0  );
	check_int( base64_char_to_6bit('K'), 10 );
	check_int( base64_char_to_6bit('Z'), 25 );
	
	check_int( base64_char_to_6bit('a'), 26 );
	check_int( base64_char_to_6bit('w'), 48 );
	check_int( base64_char_to_6bit('z'), 51 );
	
	check_int( base64_char_to_6bit('0'), 52 );
	check_int( base64_char_to_6bit('5'), 57 );
	check_int( base64_char_to_6bit('9'), 61 );
	
	check_int( base64_char_to_6bit('+'), 62 );
	check_int( base64_char_to_6bit('/'), 63 );
}

void test_base64_decode() {
	char *encoded = NULL, *decoded = NULL;
	char buffer[512] = { 0 };
	
	encoded = "cGxlYXN1cmUu";
	decoded = "pleasure.";
	check_int( base64_decode(encoded, strlen(encoded), buffer, sizeof(buffer)), (ssize_t)strlen(decoded) );
	check_int( memcmp(buffer, decoded, strlen(decoded)), 0 );
	
	encoded = "bGVhc3VyZS4=";
	decoded = "leasure.";
	check_int( base64_decode(encoded, strlen(encoded), buffer, sizeof(buffer)), (ssize_t)strlen(decoded) );
	check_int( memcmp(buffer, decoded, strlen(decoded)), 0 );
	
	encoded = "ZWFzdXJlLg==";
	decoded = "easure.";
	check_int( base64_decode(encoded, strlen(encoded), buffer, sizeof(buffer)), (ssize_t)strlen(decoded) );
	check_int( memcmp(buffer, decoded, strlen(decoded)), 0 );
	
	encoded = "YW55IGNhcm5hbCBwbGVhcw==";
	decoded = "any carnal pleas";
	check_int( base64_decode(encoded, strlen(encoded), buffer, sizeof(buffer)), (ssize_t)strlen(decoded) );
	check_int( memcmp(buffer, decoded, strlen(decoded)), 0 );
	
	encoded = "YW55IGNhcm5hbCBwbGVhc3U=";
	decoded = "any carnal pleasu";
	check_int( base64_decode(encoded, strlen(encoded), buffer, sizeof(buffer)), (ssize_t)strlen(decoded) );
	check_int( memcmp(buffer, decoded, strlen(decoded)), 0 );
	
	encoded = "YW55IGNhcm5hbCBwbGVhc3Vy";
	decoded = "any carnal pleasur";
	check_int( base64_decode(encoded, strlen(encoded), buffer, sizeof(buffer)), (ssize_t)strlen(decoded) );
	check_int( memcmp(buffer, decoded, strlen(decoded)), 0 );
}

int main() {
	run(test_base64_char_to_6bit);
	run(test_base64_decode);
	
	return show_report();
}