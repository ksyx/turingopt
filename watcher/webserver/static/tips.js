function rand_int(upper) {
  return Math.floor(Math.random() * upper);
}

function select_teaser() {
  randteaser = rand_int(teasers.length)
  document.getElementById("tipoftheday_teaser").innerText = teasers[randteaser]
}

function load_tip() {
  tiphtml = tiptexts[curtip]
  tipsource = tips[tipsourceid[curtip]]['source']
  if (tipsource != "") {
    tiphtml += " <abbr title=\"" + tipsource + "\">Source</abbr>"
  }
  document.getElementById("tipoftheday_modal_text").innerHTML = tiphtml
  $("#tipoftheday_modal").on("hidden.bs.modal", () => { select_teaser() })
}

function move_tip(delta) {
  curtip = (curtip + delta)
  if (curtip < 0) {
    curtip = tiptexts.length + curtip
  } else if (curtip >= tiptexts.length) {
    curtip %= tiptexts.length
  }
  load_tip()
}

function popup_tip_of_the_day() {
  curtip = teaser_tipid[randteaser]
  load_tip()
  $('#tipoftheday_modal').modal('show')
}

teasers = []
tipsourceid = []
tiptexts = []
teaser_tipid = []
tipid = 0
teaserid = 0

for (let source in tips) {
  cursrc = tips[source]['tips']
  for (let tip in cursrc) {
    curtip = cursrc[tip]
    tiptexts.push(curtip['details'])
    tipsourceid.push(source)
    curtip = curtip['teaser']
    for (let teaser in curtip) {
      teasers.push(curtip[teaser])
      teaser_tipid.push(tiptexts.length - 1)
    }
  }
}

document.getElementById("tipoftheday_container").outerHTML = `
  <div class="modal fade" id="tipoftheday_modal" tabindex="-1" aria-labelledby="popup" aria-hidden="true">
    <div class="modal-dialog modal-dialog-scrollable">
      <div class="modal-content">
        <div class="modal-header">
          <h1 class="modal-title fs-5">Tip of the Day</h1>
          <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
        </div>
        <div class="modal-body">
          <table class="table" style="border-color: rgb(160, 160, 160); table-layout: fixed;">
            <tr>
              <td style="width: 15%; background-color: rgb(160, 160, 160); border-color: rgb(160, 160, 160)" rowspan="2">
                <center><i class="fa-solid fa-lightbulb fa-xl" style="color: #ffffff;"></i></center>
              </td>
              <td style="border-color: rgb(160, 160, 160)"><h4>Did you know...</h4></td>
            </tr>
            <tr>
              <td style="width: 85%" id="tipoftheday_modal_text"><p>
              </p></td>
            </tr>
          </table>
        </div>
        <div class="modal-footer">
          <button type="button" class="btn" onclick="move_tip(-1)">Prev</button>
          <button type="button" class="btn" onclick="move_tip(1)">Next</button>
          <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Close</button>
        </div>
      </div>
    </div>
  </div>
`

select_teaser()
