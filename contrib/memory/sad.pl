#!/usr/bin/env perl

# Summarize the malloc/free trace from useless.stp

my %allocs;
our %tbcache;

sub free
{
	my $addr = shift;
	return unless exists $allocs{$addr};
	delete ${$allocs{$addr}->{tb}}->{allocs}{$addr};
	delete $allocs{$addr};
}

sub malloc
{
	my $size = shift;
	my $addr = shift;

	$allocs{$addr} = { addr => $addr, size => $size };
	return \$allocs{$addr}
}

sub tb
{
	my $tb = shift;
	my $addr = shift;
	our $tbid;

	$tbcache{$tb} = { tb => $tb, tbid => $tbid++, allocs => {} } unless exists $tbcache{$tb};
	$tb = \$tbcache{$tb};
	$$tb->{allocs}->{$$addr->{addr}} = $addr;
	$$addr->{tb} = $tb;
}

my $tb = '';
my $addr;
while (<STDIN>) {
	if (/^ 0x.* : .*/) {
		$tb .= $_;
		next;
	} else {
		tb ($tb, $addr);
		$tb = '';
	}

	if (/^free\((.*)\)$/) {
		free(hex $1);
	} elsif (/^malloc\((.*)\) = (.*)/) {
		$addr = malloc($1, hex $2);
	}
}

print "=== Leftover allocations by count ===\n";
printf "Count: %d, Size: %d\n%s\n", scalar keys %{$_->{allocs}}, ${[values %{$_->{allocs}}]->[0]}->{size}, $_->{tb}
	foreach sort { scalar keys %{$b->{allocs}} <=> scalar keys %{$a->{allocs}} }
		grep { %{$_->{allocs}} } %tbcache;


print "\n=== Leftover allocations by address with sizes and count  ===\n";
for $addr (sort keys %allocs) {
	$tb = $allocs{$addr}->{tb};
	printf "0x%x: %20d %8d %8d\n", $addr, $allocs{$addr}->{size}, $$tb->{tbid}, scalar keys %{$$tb->{allocs}};
}
