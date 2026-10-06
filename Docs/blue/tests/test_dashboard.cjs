const {JSDOM}=require('../.test-tools/node_modules/jsdom');
const fs=require('node:fs');
const assert=require('node:assert/strict');
const initial={connected:false,connecting:false,scanning:false,bt_ready:true,peer:'',message:'Ready',error:'',last_key:'Waiting',last_raw:'-',hold_ms:650,reports:0,decoded_reports:0,descriptor_bytes:0,mapped_fields:0,uptime_ms:1000,
  buttons:['volume_up','volume_down','next','back','play_pause'].map(id=>({id,down:false,held:false,duration_ms:0,presses:0,holds:0})),events:[],
  devices:[{name:'Satechi Media Button',mac:'DC:2C:26:08:87:B8',rssi:-66}],logs:['one','two']};
const requests=[];
let fail=false;
const dom=new JSDOM(fs.readFileSync('main/dashboard.html','utf8'),{url:'http://192.168.4.1/',runScripts:'dangerously',beforeParse(w){
  w.setTimeout=()=>1;w.clearTimeout=()=>{};
  w.fetch=async(url,options)=>{
    requests.push({url,method:options.method});
    return {ok:!fail,status:fail?409:200,json:async()=>fail?{ok:false,message:'Pairing rejected'}:url==='/api/state'?structuredClone(initial):{ok:true,message:'Connecting'}};
  };
}});
console.log('Dashboard DOM loaded');
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
  await tick();await tick();
  console.log('Initial poll completed');
  const w=dom.window,d=w.document;
  assert.equal(d.querySelectorAll('.key').length,5);
  assert.equal(d.getElementById('logs').textContent,'one\ntwo');
  d.querySelector('#devices button').click();await tick();await tick();
  assert.deepEqual(requests.at(-1),{url:'/api/connect?mac=DC%3A2C%3A26%3A08%3A87%3AB8',method:'POST'});
  assert.equal(d.getElementById('error').hidden,true);
  fail=true;d.querySelector('#devices button').click();await tick();await tick();
  assert.equal(d.getElementById('error').textContent,'Pairing rejected');
  assert.equal(d.getElementById('error').hidden,false);
  fail=false;
  const s=structuredClone(initial);
  s.connected=true;s.buttons[0].down=true;s.buttons[0].held=true;s.buttons[0].holds=1;s.buttons[0].duration_ms=800;
  s.events=[{seq:1,time_ms:10,key:'play_pause',name:'Play / Pause',action:'PRESS',duration_ms:80},{seq:2,time_ms:900,key:'volume_up',name:'Volume +',action:'HOLD',duration_ms:800}];
  s.devices[0].name='<img src=x onerror=alert(1)>';
  w.render(s);
  assert.match(d.getElementById('key-volume_up').className,/hold/);
  assert.equal(d.querySelector('#key-volume_up .key-state').textContent,'HOLD 800 ms');
  assert.equal(d.querySelectorAll('#events li').length,1);
  assert.match(d.querySelector('#key-play_pause .key-state').textContent,/PRESS/);
  assert.equal(d.querySelector('#devices img'),null);
  assert.equal(d.querySelector('#devices button').disabled,true);
  assert.equal(d.getElementById('disconnect').disabled,false);
  s.buttons[0].down=false;s.buttons[0].held=false;
  s.events.push({seq:3,time_ms:1200,key:'volume_up',name:'Volume +',action:'HOLD END',duration_ms:1100});w.render(s);
  assert.equal(d.querySelector('#key-volume_up .key-state').textContent,'HOLD');
  assert.equal(d.querySelectorAll('#events li').length,2);
  assert.match(d.querySelector('#events li').textContent,/HOLD · 1100 ms/);
  assert.doesNotMatch(d.getElementById('events').textContent,/HOLD END|800 ms|DOWN/);
  console.log('PASS dashboard Connect request, visible API errors, five indicators, retained taps, holds/releases, safe names');
  dom.window.close();
})().catch(error=>{console.error(error);dom.window.close();process.exitCode=1;});
