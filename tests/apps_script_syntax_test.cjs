const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

const folder=path.join(__dirname,'../cloud/apps_script');
const files=fs.readdirSync(folder).filter(name=>name.endsWith('.gs')).sort();
const source=files.map(name=>`// ${name}\n${fs.readFileSync(path.join(folder,name),'utf8')}`).join('\n');
new vm.Script(source,{filename:'combined Apps Script project'});
assert.throws(()=>new vm.Script(source+'\nconst MAX_BATCH=99;'),SyntaxError);
console.log(`PASS Apps Script global parse: ${files.join(', ')}; duplicate MAX_BATCH rejected`);
