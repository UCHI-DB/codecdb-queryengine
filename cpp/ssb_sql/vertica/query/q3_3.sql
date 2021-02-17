select c_city, s_city, d_year, sum(lo_revenue) as lo_revenue
from lineorder
         left join dates on lo_orderdate = d_datekey
         left join customer on lo_custkey = c_custkey
         left join supplier on lo_suppkey = s_suppkey
where (c_city='UNITED KI1' or c_city='UNITED KI5')
  and (s_city='UNITED KI1' or s_city='UNITED KI5')
  and d_year >= 1992 and d_year <= 1997
group by c_city, s_city, d_year
order by d_year asc, lo_revenue desc;

SELECT last_statement_duration_us / 1000000.0 last_statement_duration_seconds
FROM current_session;
