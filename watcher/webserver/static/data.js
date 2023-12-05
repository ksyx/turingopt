function form_table(data, periods) {
  var result = []
  var raw = data
  var problems = {}
  const textcolumns = [
    'Job',
    'Step',
    'User',
    'Name',
    'Source',
    'Node',
    'App',
    'GPUCPUEntryType',
  ]
  var columns = {'Period': 'datetime'}
  for (let col in textcolumns) {
    columns[textcolumns[col]] = 'string'
  }
  for (let period in raw) {
    var curperiod = raw[period]['data']
    var curperiod_datetime = periods[period]['updated'] * 1000
    var agg_level_has_problem = [{}, {}]
    for (let user in curperiod) {
      var curuser = curperiod[user]
      var jobstep_name = {}
      curuser['JobInfo'].forEach((info) => {
        jobstep_name[info['Job']] = info['Name']
      });
      var agg_level = (info) => {return info['Step'] == "null" ? 1 : 0}
      for (let entry in curuser) {
        var curentry = curuser[entry]
        /*
        if (curentry.length == undefined) {
          curentry = [curentry]
        }
        */

        curentry.forEach((info) => {
          for (let col in info) {
            if (info[col] != null && columns[col] != 'string') {
              if (entry == 'Problems' && col != 'Job' && col != 'Step') {
                columns[col] = 'integer'
                problems[col] = 1
                info[col] = 100
                agg_level_has_problem[agg_level(info)][col] = true
              } else if (columns[col] != 'float') {
                if (!Number.isInteger(info[col])
                      || BigInt(String(info[col])) > 2147483647) {
                  columns[col] = 'float'
                } else {
                  columns[col] = 'integer'
                }
              }
            }

            if (col == 'JobLength' && info[col] < 0) {
              info[col] = null
            }
          }
          if (entry != 'JobInfo') {
            info['Name'] = jobstep_name[info['Job']]
          }
          info['User'] = user
          info['Source'] = entry
          info['Period'] = curperiod_datetime
          function stepid_mapping(id) {
            switch (id) {
              case -3: return 'pending';
              case -4: return 'extern';
              case -5: return 'batch';
              case -6: return 'interactive';
              default: return String(id);
            }
          }
          info['Step'] = stepid_mapping(info['Step'])
        })
        result = result.concat(curentry)
      }
    }
    result.forEach((info) => {
      for (let col in columns) {
        if (info[col] == undefined) {
          if (info['Source'] == 'Problems'
              && problems[col] != undefined
              && agg_level_has_problem[agg_level(info)][col]) {
            info[col] = 0
          } else {
            info[col] = null
          }
        }
      }
    })
  }
  return {schema: columns, data: result}
}
