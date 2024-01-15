function produce_order(data, dedup, name_mapping, available_periods) {
  conn = {}
  rootnode = "greeting"
  depth = {}
  secid = {}
  is_common = {}
  depth[rootnode] = 1
  databackup = data
  data = data['user_content']['toc']
  conn_rev = {}
  users = {}
  for (let entry in data) {
    users[entry] = 1
    entry = data[entry]
    jq = $(entry).find("td>a, td>ul>li>a")
    prev = rootnode
    for (i = 0; i < jq.length; i++) {
      val = jq[i]["attributes"]["href"].textContent.substr(1)
      if (jq[i]["parentElement"]['tagName'] == "TD") {
        dep = 1
      } else {
        dep = 2
      }
      if (conn[val] == undefined) {
        conn[val] = {}
      }
      if (conn_rev[prev] == undefined) {
        conn_rev[prev] = {}
      }
      depth[val] = dep
      conn[val][prev] = 1
      conn_rev[prev][val] = 1
      prev = val
    }
  }
  userlist = []
  for (let user in users) {
    userlist.push(user)
  }
  li = [rootnode]
  finalorder = []
  while (li.length) {
    cur = li[0]
    li = li.slice(1)
    finalorder.push(cur)
    for (let next in conn_rev[cur]) {
      delete conn[next][cur]
      if (!Object.keys(conn[next]).length) {
        li.push(next)
      }
    }
  }
  lastdepth = 1
  final = []
  missingcontent = {}
  for (let item in finalorder) {
    it = finalorder[item]
    d = depth[it]
    if (d == 1) {
      final.push(it)
    } else {
      if (lastdepth == 1) {
        final.push([it])
      } else {
        final[final.length - 1].push(it)
      }
    }
    missingcontent[item] = {}
    lastdepth = d
  }
  console.log(final)
  data = databackup
  sectionhtml = ""
  tochtml = ""
  cnt = 0
  for (i = 0; i < finalorder.length; i++) {
    sec = finalorder[i]
    commonid = data['common_content'][sec]
    sectionhtml += "<section>"
    if (commonid != undefined) {
      sectionhtml += dedup['content_data'][commonid]
    } else {
      cur = data['user_content'][sec]
      console.log('users')
      if (cur != undefined) {
        for (user in userlist) {
          username = userlist[user]
          str = cur[username]
          missingcontent[i][user] = str == undefined
          if (str == undefined) {
            str = "<h4><a href=\"#null\">" + name_mapping[sec] + "</a></h4>"
            str += "<p>no data available</p>"
          }
          sectionhtml += "<section>" + str + "</section>"
        }
      }
    }
    sectionhtml += "</section>"
    curdepth = depth[sec]

    if (i + 1 == finalorder.length) {
      nextdepth = 1
    } else {
      nextdepth = depth[finalorder[i + 1]]
    }
    function makelink(classname) {
      classname = classname + ' rounded'
      return '<a class="' + classname + '" href="#' + sec + '" id="toc-' + i + '">' + name_mapping[sec] + '</a>'
    }
    if (curdepth == 1) {
      additionalclass = "btn-nosubentry"
      if (nextdepth > curdepth) {
        additionalclass = ""
        tochtml += '<li class="mb-1">'
        tochtml += '<button class="btn btn-toggle d-inline-flex align-items-center'
        tochtml += ' rounded border-0" data-bs-toggle="collapse" aria-expanded="true"'
        tochtml += ' data-bs-target="#' + sec + '-collapse"></button>'
      } else {
        tochtml += '<div>'
      }
      tochtml += makelink('btn btn-link border-0 d-inline-flex align-items-center ' + additionalclass)
      if (nextdepth > curdepth) {
        tochtml += '<div class="collapse show" id="' + sec + '-collapse">'
        tochtml += '<ul class="btn-toggle-nav list-unstyled fw-normal pb-1 small">'
      } else {
        tochtml += '</div>'
      }
    } else if (curdepth == 2) {
      tochtml += '<li>' + makelink('link-body-emphasis d-inline-flex text-decoration-none') + '</li>'
      if (nextdepth == 1) {
        tochtml += '</ul></div></li>'
      }
    }
    secid[sec] = cnt
    if (commonid != undefined) {
      is_common[cnt] = 1
    }
    cnt++
  }
  return {sectionHtml: sectionhtml, tocHtml: tochtml,
          userSelectHtml: produce_selector_html(userlist),
          periodSelectHtml: produce_selector_html(available_periods)}
}

zoomin_class = 'fa-magnifying-glass'
zoomout_class = 'fa-share fa-flip-horizontal'
zoomin_title = 'Zoom-In Table'
zoomout_title = 'Zoom-Out Table'
svg_backup_location = 'Zoom SVG Backup'

function zoomtable() {
  if (tbl_jq.length) {
    if (is_zoomin) {
      jqwidth = jq.width()
      tblwidth = tbl_jq.width()
      table_zoom_ratio = 0.98 * jqwidth / tblwidth
      tbl_jq.css("transform", "scale(" + table_zoom_ratio + ")")
      jq.css("height", tbl_jq.height() * table_zoom_ratio)
      if (tblwidth > jqwidth) {
        customcontrol_jq.css("display", "block")
      }
    } else {
      tbl_jq.css("transform", "unset")
    }
  } else {
    customcontrol_jq.css("display", "none")
  }
  is_zoomin = !is_zoomin
}

function reloadpage(args) {
  cururl = new URL(window.location.href);
  for (let item in args) {
    if (args[item] != undefined) {
      cururl.searchParams.set(item, args[item])
    }
  }
  console.log(cururl.href, args)
  window.location.href = cururl.href;
}

function zoomtable_button() {
  function gettitle(str) {
    return 'button[title="' + str + '"]'
  }
  from = zoomin_title
  to = zoomout_title
  zoomin = true
  if (!$(gettitle(from)).length) {
    t_ = from
    from = to
    to = t_
    zoomin = false
  }
  console.log(from, to)
  zoom_from_jq = $(gettitle(from))
  zoom_from_jq.attr("title", to)
  to = svg_backup_location
  zoom_to_jq = $(gettitle(to))
  t_ = zoom_from_jq.html()
  zoom_from_jq.html(zoom_to_jq.html())
  zoom_to_jq.html(t_)
  zoomtable()
}

event_pos = undefined

function initialize_revealjs() {
  lasteventmouseover = undefined
  function jq_td_mouseleave(event) {
    if (lasteventmouseover == "UL" || event_pos == undefined) {
      return
    }
    lasteventmouseover = event.currentTarget.tagName
    if (event.pageX < event_pos.left || event.pageY < event_pos.top
        || event.pageX > event_pos.left + event_width
        || event.pageY > event_pos.top + event_height) {
      event_jq.css("transform", "unset")
      event_jq.css("z-index", "unset")
      event_jq.css("position", "inherit")
      event_jq.css("background", "inherit")
      for (i = 0; i < event_jq.length; i++) {
        event_jq[i].removeEventListener("mousemove", jq_td_mouseleave_handler)
      }
    }
  }
  function jq_td_mouseover(event) {
    if (!event.ctrlKey) {
      return
    }
    lasteventmouseover = ""
    event_jq = $(event.currentTarget)
    if (event_jq.text() == "") {
      return
    }
    event_pos = event_jq.offset()
    event_width = event_jq.outerWidth() * table_zoom_ratio
    event_height = event_jq.outerHeight() * table_zoom_ratio
    if (event_jq.children().length && event_jq.children()[0]['tagName'] != "A") {
      event_jq = event_jq.children()
    }
    cell_zoom_ratio = 1.0 / table_zoom_ratio
    event_jq.css("transform", "scale(" + cell_zoom_ratio + ")")
    event_jq.css("background", "white")
    event_jq.css("z-index", "999")
    event_jq.css("position", "relative")
    for (i = 0; i < event_jq.length; i++) {
      cur = event_jq[i]
      cur.addEventListener("mousemove", jq_td_mouseleave_handler, false)
      cur = $(cur)
      curoff = event_jq.position()
      function set_offset(parentlength, curcorner, curlength, type) {
        curcorner *= cell_zoom_ratio
        curlength *= cell_zoom_ratio
        off = parentlength - (curcorner + curlength)
        if (curcorner < 0) {
          off = -curcorner / cell_zoom_ratio
          if (type == "Y") {
            off /= cell_zoom_ratio
          }
        } else if (off > 0) {
          off = 0
        }
        if (off != 0) {
          cur.css("transform",
                  cur.css("transform") + " translate" + type + "(" + off + "px)")
        }
        return off
      }
      tbloff = tbl_jq.position()
      set_offset(tbl_jq[0]['clientWidth'], curoff.left - tbloff.left, cur.outerWidth(), "X")
      set_offset(tbl_jq[0]['clientHeight'], curoff.top - tbloff.top, cur.outerHeight(), "Y")
    }
  }
  jq_td_handler = (event, f) => {
    if (!is_zoomin &&
        ((event.target.tagname != "th" && event.target.tagName != "td")
         || event.target == event.currentTarget)) {
      f(event)
    }
  }
  jq_td_mouseover_handler = (event) => {jq_td_handler(event, jq_td_mouseover)}
  jq_td_mouseleave_handler = (event) => {jq_td_handler(event, jq_td_mouseleave)}

  function updateSelectValue(select, val, performaction) {
    select.selectpicker('val', String(val));
    if (performaction) {
      select[0].onchange()
    }
  }

  Reveal.initialize({
    embedded: true,
    // hash: true,
    center: false,
    disableLayout: true,
    controlsBackArrows: 'visible',
    autoAnimate: false,
    transition: 'none',
    hideInactiveCursor: false,
    progress: false,
    plugins: [RevealCustomControls],
    keyboard: {
      27: null, // ESC
      38: 'left', // Up
      75: 'left', // k
      87: 'left', // w

      40: 'right', // Down
      74: 'right', // j
      83: 'right', // s

      37: 'up', // Left
      72: 'up', // h
      65: 'up', // a

      39: 'down', // Right
      76: 'down', // l
      68: 'down', // d
    },
    customcontrols: {
      controls: [
        {
          id: 'table-zoom',
          title: 'Zoom-In Table',
          icon: '<i class="fa ' + zoomin_class + ' fa-xl"></i>',
          action: 'zoomtable_button()'
        },
        {
          id: 'svg-backup',
          title: svg_backup_location,
          icon: '<i class="fa ' + zoomout_class + ' fa-xl"></i>',
          action: ''
        }
      ]
    }
  }).then( () => {
    lastrevealstate = Reveal.getState()
    mapping = { "left": "up", "right": "down" }
    for (let t in mapping) {
      mapping[mapping[t]] = t
    }
    for (let t in mapping) {
      var cls = "navigate-" + t
      $("." + cls).removeClass(cls).addClass("nav" + mapping[t])
      console.log(cls, "nav" + mapping[t])
    }
    for (let t in mapping) {
      var cls = "nav" + t
      $("." + cls).removeClass(cls).addClass("navigate-" + t)
    }
    $("#popup_modal").on("shown.bs.modal", () => {
      if (tagName == 'TH') {
        $("#popup_modal_message").find('table').addClass("table").addClass("border-black")
        elem = $("#popup_modal_message").find('a[name="' + sec + '"]')
        elem.closest('th').css("background-color", "var(--bs-info-bg-subtle)")
        elem[0].scrollIntoView()
      }
      window.location.hash = "#popup"
    })
    addEventListener("hashchange", (event) => {
      sec = location.hash.slice(1)
      hashtarget = $('a[name="' + sec + '"]')
      if (hashtarget.length) {
        hashtarget = hashtarget.parent()
        tagName = hashtarget[0]['tagName']
        if (tagName[0] == 'H' || secid[sec] != undefined) {
          from_hashchange = true
          reveal_state = Reveal.getState()
          target_uid = 0
          target_secid = secid[sec]
          if (!is_common[reveal_state['indexh']]) {
            cur_user = reveal_state['indexv']
          }
          if (cur_user != undefined && !is_common[target_secid]) {
            target_uid = cur_user
          } else {
            target_uid = 0
          }
          console.log({
            indexh: target_secid,
            indexv: target_uid,
            indexf: undefined,
            paused: false,
            overview: false
          })
          Reveal.setState({
            indexh: target_secid,
            indexv: target_uid,
            indexf: undefined,
            paused: false,
            overview: false
          })
          window.location.hash = "#null"
          window.scrollTo({top: 0});
        } else if (tagName == 'P' || tagName == 'TH') {
          if (tagName == 'P') {
            title = hashtarget.parent().parent().find("th").text()
            message = hashtarget.html().slice(hashtarget.children()[0].outerHTML.length)
          } else {
            title = hashtarget[0].innerText
            message = hashtarget.parent().parent().parent()[0]['outerHTML']
          }
          showmessage(title, message)
        }
      }
    }, false);
    user_select_elem = document.getElementById('user_select')
    user_select_jq = $('#user_select')
    user_select_elem.onchange = function() {
      cur_user = user_select_elem.value
      reveal_state = Reveal.getState()
      if (!is_common[reveal_state['indexh']]) {
        reveal_state['indexv'] = cur_user
        from_hashchange = true
        Reveal.setState(reveal_state)
      }
    }
    period_select_elem = document.getElementById('period_select')
    period_select_jq = $('#period_select')
    period_select_elem.onchange = function() {
      reveal_state = Reveal.getState()
      reloadpage({
        'period': period_select_elem.value,
        'section': finalorder[reveal_state.indexh],
        'user': userlist[reveal_state.indexv]
      })
    }
    var req = new URL(window.location.href).searchParams;
    requser = req.get('user')
    reqsection = req.get('section')
    reqperiod = req.get('period')
    console.log(requser, reqsection)
    if (reqperiod != null) {
      updateSelectValue(period_select_jq, reqperiod, false)
    }
    if (requser != null || reqsection != null) {
      lastrevealstate = Reveal.getState()
      reveal_state = Reveal.getState()
      if (reqsection != null && secid[reqsection] != undefined) {
        window.location.hash = '#' + reqsection
      }
      if (requser != null) {
        for (user in userlist) {
          if (userlist[user] == requser) {
            updateSelectValue(user_select_jq, user, true)
            break
          }
        }
      }
    }
    lastrevealstate = Reveal.getState()
  });
  is_zoomin = false
  jq_td = undefined
  event_jq = undefined
  from_hashchange = false
  cur_user = undefined
  table_zoom_ratio = undefined
  Reveal.on('slidechanged', event => {
    is_common_info = is_common[event.indexh]
    $("#toc-" + lastrevealstate.indexh).removeClass('current-page')
    $("#toc-" + event.indexh).addClass('current-page')
    if (!from_hashchange) {
      cur_user = undefined
      indexv_diff = event.indexv - lastrevealstate.indexv
      if (!is_common_info && lastrevealstate.indexh == event.indexh
          && (indexv_diff == -1 || indexv_diff == 1)) {
        i = event.indexv
        while (i >= 0 && i < userlist.length && missingcontent[event.indexh][i]) {
          // console.log(missingcontent[event.indexh][i], i, indexv_diff)
          i += indexv_diff
        }
        if (i >= 0 && i < userlist.length) {
          if (i != event.indexv) {
            reveal_state = Reveal.getState()
            reveal_state.indexv = i
            Reveal.setState(reveal_state)
            return
          }
        } else {
          if (indexv_diff == 1) {
            word = 'last'
          } else {
            word = 'first'
          }
          showmessage('No more data available',
                      'Already reached the ' + word + ' user with this info.')
          reveal_state = lastrevealstate
          Reveal.setState(reveal_state)
        }
      }
    }
    lastrevealstate = Reveal.getState()
    if (!is_common_info) {
      updateSelectValue(user_select_jq, event.indexv, false)
    }
    from_hashchange = false
    if (jq_td != undefined) {
      for (i = 0; i < jq_td.length; i++) {
        jq_td[i].removeEventListener("mouseover", jq_td_mouseover_handler)
        jq_td[i].removeEventListener("mousemove", jq_td_mouseleave_handler)
      }
    }
    jq = $(event.currentSlide)
    jq_td = jq.find("td, th")
    tbl_jq = jq.find("table")
    if (is_zoomin) {
      zoomtable_button()
    }
    is_zoomin = true
    zoomtable()
    if (table_zoom_ratio < 0.6) {
      for (i = 0; i < jq_td.length; i++) {
        jq_td[i].addEventListener("mouseover", jq_td_mouseover_handler, false)
        jq_td[i].addEventListener("mouseleave", jq_td_mouseleave_handler, false)
      }
    }
  });

  customcontrol_jq = $("#customcontrols")
  $("#svg-backup").css("display", "none");
  customcontrol_jq.css("display", "none");
}
