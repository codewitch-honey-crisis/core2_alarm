var timerId = null;
function resetAll() {
    if(!(timerId == null)) {
        clearInterval(timerId);
    }
    
    fetch("./api?set")
    .then(response => response.json())
    .then(alarms => {
        for(var i = 0;i<alarms.status.length;++i) {
            const id = "a"+i;
            const cb = document.getElementById(id);
            cb.checked = alarms.status[i];
        }
    })
    .catch(error => console.error("Error fetching JSON data:", error));
    timerId = setInterval(refreshSwitches,500);
}
function refreshSwitches(write) {
    var url = "./api/";
    if(!(timerId == null)) {
        clearInterval(timerId);
    }
    if(write==true) {
        url+="?set"
        var cbs = document.getElementsByTagName("input");
        for(var i = 0;i<cbs.length;++i) {
            const id = "a"+i;
            const cb = document.getElementById(id);
            if(cb!=undefined) {
                if(cb.checked) {
                    url+=("&a="+i);
                } 
            }
        }
    }
    fetch(url)
        .then(response => response.json())
        .then(alarms => {
            for(var i = 0;i<alarms.status.length;++i) {
                const id = "a"+i;
                const cb = document.getElementById(id);
                cb.checked = alarms.status[i];
            }
        })
        .catch(error => console.error("Error fetching JSON data:", error));
    timerId = setInterval(refreshSwitches,500);
}
