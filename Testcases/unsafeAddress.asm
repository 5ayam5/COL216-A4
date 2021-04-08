main:
	addi $t1, $t1, 1000
	sw $t1, 1000
	lw $t0, 1000
	lw $s1, 0($t0)
	addi $t0, $t0, 4
	lw $s3, 0($t0)
