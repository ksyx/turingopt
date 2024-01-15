function showmessage(title, message) {
  document.getElementById("popup_modal_title").innerText = title
  document.getElementById("popup_modal_message").innerHTML = message
  $("#popup_modal").modal('show')
}

function produce_selector_html(list) {
  html = ""
  for (item in list) {
    html += '<option value="' + item + '">' + list[item] + '</option>'
  }
  return html
}

function load_selector_options(html, id) {
  if (html != "") {
    document.getElementById(id).innerHTML = html;
    $('#' + id).selectpicker('destroy');
    $('#' + id).selectpicker('render');
  } else {
    $("#" + id).parent().css("display", "none")
  }
}

async function verify_response(response) {
if (!response.ok) {
  var title = response.status + " " + response.statusText
  var msg = "<p>Failed to obtain result.</p>"
    if (response.status == 401) {
      msg += "<p>Login may be expired, please refresh the page.</p>"
    } else {
      try {
        var body = await response.json()
        msg += "<p>Details: " + body["msg"] + "</p>"
      } catch (e) {}
    }
    showmessage(title, msg)
    return false
  }
  return true
}

async function fetch_periods() {
  const response = await fetch("/api/periods", {
    method: "GET",
    headers: {
      "Accept": "application/json"
    }
  })
  if (!await verify_response(response)) {
    return
  }
  var data = await response.json()
  data = data["payload"]
  for (let val in data) {
    var cur = data[val]
    var date = new Date(cur["updated"] * 1000)
    cur["name"] = date.toLocaleDateString(undefined, {
      year: 'numeric',
      month: 'short',
      day: 'numeric',
    })
  }
  return data
}

function map_periods(data) {
  var ret = {}
  for (let val in data) {
    ret[val] = data[val]["name"]
  }
  return ret
}
