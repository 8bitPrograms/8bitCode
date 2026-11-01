return CopySqlLanguage({
	"pgsql",
	"psql",
}, {
	[0] = "select insert update delete merge from where and or not null is in like ilike similar between join inner left right full outer cross on using group by order having union all distinct as create alter drop table view index sequence trigger function returns language begin end if then else elsif loop while for return into values set commit rollback grant revoke exception when raise exists case cast true false default constraint primary key foreign references check add column with lateral returning conflict do nothing update generated always identity materialized unlogged tablesample window over partition filter within except intersect offset limit fetch rows only",
	[1] = "coalesce nullif greatest least now current_date current_timestamp current_time length substring position trim ltrim rtrim to_char to_number to_date count sum avg min max string_agg json_agg jsonb_agg",
	[4] = "smallint integer bigint serial bigserial numeric decimal real float4 float8 boolean text varchar char bytea jsonb json uuid xml timestamptz timestamp date time interval citext int2 int4 int8 money bool",
}, {
	["lexer.sql.allow.dotted.word"] = "1",
	["sql.backslash.escapes"] = "1",
	["fold.sql.at.else"] = "1",
})
