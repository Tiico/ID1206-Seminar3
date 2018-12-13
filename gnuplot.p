set term png
set output "greenplot.png"
set xlabel "Productions"
set ylabel "Time (ms)"
plot 'data.dat' using 1:2 title 'Green' with lines, \
'data.dat' using 1:3 title 'Pthreads' with lines
