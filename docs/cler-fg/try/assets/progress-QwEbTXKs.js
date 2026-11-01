const u="3144c72eddf55bb17b0b84ae99438bff2dccaa8f9acf659a2bb6bda5cc2eaeea",f="0.3.0",m="desktop_examples/adsb_receiver.cpp";const g=[],x=[{function:"main",call_offset:8479,span:{start:8479,end:8704},flowgraph_var:"flowgraph",blocks:[{var:"source",type_text:"std::optional<SelectableSourceBlock>",type_name:"SelectableSourceBlock",alias:null,template_args:[],ctor_args:[{text:'"Source"',span:{start:7133,end:7141}},{text:"use_soapy",span:{start:7155,end:7164}},{text:'use_soapy ? "" : source_arg',span:{start:7178,end:7205}},{text:"ADSB_FREQ_HZ",span:{start:7265,end:7277}},{text:"SAMPLE_RATE_HZ",span:{start:7291,end:7305}},{text:"GAIN_DB",span:{start:7319,end:7326}}],display_name:"Source",in_graph:!0,span:{start:7042,end:7086},editable:!1,read_only_reason:"optional_emplace_declaration"},{var:"iq2mag",type_text:"IQToMagnitudeBlock",type_name:"IQToMagnitudeBlock",alias:null,template_args:[],ctor_args:[{text:'"IQ to Magnitude"',span:{start:7834,end:7851}},{text:"SAMPLE_RATE_HZ",span:{start:7853,end:7867}}],display_name:"IQ to Magnitude",in_graph:!0,span:{start:7808,end:7869},editable:!0,read_only_reason:null},{var:"decoder",type_text:"ADSBDecoderBlock",type_name:"ADSBDecoderBlock",alias:null,template_args:[],ctor_args:[{text:'"ADSB Decoder"',span:{start:8089,end:8103}},{text:"decoder_mode",span:{start:8105,end:8117}}],display_name:"ADSB Decoder",in_graph:!0,span:{start:8064,end:8119},editable:!0,read_only_reason:null},{var:"null_sink",type_text:"SinkNullBlock<uint16_t>",type_name:"SinkNullBlock",alias:null,template_args:[{text:"uint16_t",resolved:null,span:{start:8139,end:8147}}],ctor_args:[{text:'"Null Sink"',span:{start:8159,end:8170}}],display_name:"Null Sink",in_graph:!1,span:{start:8125,end:8172},editable:!0,read_only_reason:null},{var:"aggregator",type_text:"ADSBAggregateBlock",type_name:"ADSBAggregateBlock",alias:null,template_args:[],ctor_args:[{text:'"ADSB Map"',span:{start:8217,end:8227}},{text:"initial_lat",span:{start:8237,end:8248}},{text:"initial_lon",span:{start:8250,end:8261}},{text:"on_aircraft_update",span:{start:8271,end:8289}},{text:"nullptr",span:{start:8299,end:8306}}],display_name:"ADSB Map",in_graph:!0,span:{start:8178,end:8391},editable:!0,read_only_reason:null}],runners:[{index:0,block:"source",block_expr:"&*source",may_block:!1,form:"inline",span:{start:8517,end:8556},editable:!1,read_only_reason:"unsupported_block_expression"},{index:1,block:"iq2mag",block_expr:"&iq2mag",may_block:!1,form:"inline",span:{start:8566,end:8605},editable:!0,read_only_reason:null},{index:2,block:"decoder",block_expr:"&decoder",may_block:!1,form:"inline",span:{start:8615,end:8658},editable:!0,read_only_reason:null},{index:3,block:"aggregator",block_expr:"&aggregator",may_block:!1,form:"inline",span:{start:8668,end:8698},editable:!0,read_only_reason:null}],edges:[{from:"source",to:"iq2mag",port:{name:"in",index:null,kind:"field"},runner_index:0,arg_index:1,text:"&iq2mag.in",span:{start:8545,end:8555},editable:!1,read_only_reason:"unsupported_block_expression",sample_type:"std::complex<int16_t>",source_type:"std::complex<int16_t>",type_conflict:!1},{from:"iq2mag",to:"decoder",port:{name:"in",index:null,kind:"field"},runner_index:1,arg_index:1,text:"&decoder.in",span:{start:8593,end:8604},editable:!0,read_only_reason:null,sample_type:"uint16_t",source_type:"uint16_t",type_conflict:!1},{from:"decoder",to:"aggregator",port:{name:"in",index:null,kind:"field"},runner_index:2,arg_index:1,text:"&aggregator.in",span:{start:8643,end:8657},editable:!0,read_only_reason:null,sample_type:"mode_s_msg",source_type:"mode_s_msg",type_conflict:!1}],config:{var:null,source:"absent",assignments:[],run_call_span:{start:8765,end:8780},editable:!0,read_only_reason:null},gui:{var:"gui",span:{start:9058,end:9124},legacy:!1},unresolved:[],editable:!1,read_only_reason:"site_has_read_only_elements"}],k={sha256:u,version:f,file:m,has_errors:!1,errors:g,sites:x},h="0e85b7f94049ee7dfef40ed38850e18ffc53c2be9bf57656e1abcf3cf113ddec",y="0.3.0",b="desktop_examples/hello_world.cpp",w=!1,S=[],B=[{function:"main",call_offset:926,span:{start:926,end:1196},flowgraph_var:"flowgraph",blocks:[{var:"source1",type_text:"SourceCWBlock<float>",type_name:"SourceCWBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:460,end:465}}],ctor_args:[{text:'"CWSource"',span:{start:475,end:485}},{text:"1.0f",span:{start:487,end:491}},{text:"1.0f",span:{start:493,end:497}},{text:"SPS",span:{start:499,end:502}}],display_name:"CWSource",in_graph:!0,span:{start:446,end:504},editable:!0,read_only_reason:null},{var:"source2",type_text:"SourceCWBlock<float>",type_name:"SourceCWBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:546,end:551}}],ctor_args:[{text:'"CWSource2"',span:{start:561,end:572}},{text:"1.0f",span:{start:574,end:578}},{text:"20.0f",span:{start:580,end:585}},{text:"SPS",span:{start:587,end:590}}],display_name:"CWSource2",in_graph:!0,span:{start:532,end:592},editable:!0,read_only_reason:null},{var:"throttle",type_text:"ThrottleBlock<float>",type_name:"ThrottleBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:611,end:616}}],ctor_args:[{text:'"Throttle"',span:{start:627,end:637}},{text:"SPS",span:{start:639,end:642}}],display_name:"Throttle",in_graph:!0,span:{start:597,end:644},editable:!0,read_only_reason:null},{var:"adder",type_text:"AddBlock<float, 2>",type_name:"AddBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:658,end:663}},{text:"2",resolved:"2",span:{start:665,end:666}}],ctor_args:[{text:'"Adder"',span:{start:674,end:681}}],display_name:"Adder",in_graph:!0,span:{start:649,end:683},editable:!0,read_only_reason:null},{var:"plot",type_text:"PlotTimeSeriesBlock",type_name:"PlotTimeSeriesBlock",alias:null,template_args:[],ctor_args:[{text:'"Hello World Plot"',span:{start:723,end:741}},{text:'{"Added Sources"}',span:{start:751,end:768}},{text:"SPS",span:{start:778,end:781}},{text:"3.0f",span:{start:791,end:795}}],display_name:"Hello World Plot",in_graph:!0,span:{start:689,end:825},editable:!0,read_only_reason:null}],runners:[{index:0,block:"source1",block_expr:"&source1",may_block:!1,form:"inline",span:{start:964,end:1005},editable:!0,read_only_reason:null},{index:1,block:"source2",block_expr:"&source2",may_block:!1,form:"inline",span:{start:1015,end:1056},editable:!0,read_only_reason:null},{index:2,block:"adder",block_expr:"&adder",may_block:!1,form:"inline",span:{start:1066,end:1105},editable:!0,read_only_reason:null},{index:3,block:"throttle",block_expr:"&throttle",may_block:!1,form:"inline",span:{start:1115,end:1156},editable:!0,read_only_reason:null},{index:4,block:"plot",block_expr:"&plot",may_block:!1,form:"inline",span:{start:1166,end:1190},editable:!0,read_only_reason:null}],edges:[{from:"source1",to:"adder",port:{name:"in",index:0,kind:"indexed_field"},runner_index:0,arg_index:1,text:"&adder.in[0]",span:{start:992,end:1004},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"source2",to:"adder",port:{name:"in",index:1,kind:"indexed_field"},runner_index:1,arg_index:1,text:"&adder.in[1]",span:{start:1043,end:1055},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"adder",to:"throttle",port:{name:"in",index:null,kind:"field"},runner_index:2,arg_index:1,text:"&throttle.in",span:{start:1092,end:1104},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"throttle",to:"plot",port:{name:"in",index:0,kind:"indexed_field"},runner_index:3,arg_index:1,text:"&plot.in[0]",span:{start:1144,end:1155},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1}],config:{var:null,source:"absent",assignments:[],run_call_span:{start:1203,end:1218},editable:!0,read_only_reason:null},gui:{var:"gui",span:{start:1224,end:1290},legacy:!1},unresolved:[],editable:!0,read_only_reason:null}],v={sha256:h,version:y,file:b,has_errors:w,errors:S,sites:B},P="129e3e3513f9d5c12302964d313bfe3bb80d56c7c7a1b673db1183caff6c0792",C="0.3.0",R="desktop_examples/mass_spring_damper.cpp",I=!1,z=[],T=[{function:"main",call_offset:21922,span:{start:21922,end:22300},flowgraph_var:"flowgraph",blocks:[{var:"controller",type_text:"ControllerBlock",type_name:"ControllerBlock",alias:null,template_args:[],ctor_args:[{text:'"Controller"',span:{start:21081,end:21093}}],display_name:"Controller",in_graph:!0,span:{start:21054,end:21095},editable:!0,read_only_reason:null},{var:"throttle",type_text:"ThrottleBlock<float>",type_name:"ThrottleBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:21114,end:21119}}],ctor_args:[{text:'"Throttle"',span:{start:21130,end:21140}},{text:"SPS",span:{start:21142,end:21145}}],display_name:"Throttle",in_graph:!0,span:{start:21100,end:21147},editable:!0,read_only_reason:null},{var:"plant",type_text:"PlantBlock",type_name:"PlantBlock",alias:null,template_args:[],ctor_args:[{text:'"Plant"',span:{start:21169,end:21176}}],display_name:"Plant",in_graph:!0,span:{start:21152,end:21178},editable:!0,read_only_reason:null},{var:"root_locus",type_text:"RootLocusBlock",type_name:"RootLocusBlock",alias:null,template_args:[],ctor_args:[{text:'"RootLocus"',span:{start:21209,end:21220}},{text:"&controller",span:{start:21222,end:21233}}],display_name:"RootLocus",in_graph:!0,span:{start:21183,end:21235},editable:!0,read_only_reason:null},{var:"fanout",type_text:"FanoutBlock<float>",type_name:"FanoutBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:21253,end:21258}}],ctor_args:[{text:'"Fanout"',span:{start:21267,end:21275}},{text:"2",span:{start:21277,end:21278}}],display_name:"Fanout",in_graph:!0,span:{start:21241,end:21280},editable:!0,read_only_reason:null},{var:"plot",type_text:"PlotTimeSeriesBlock",type_name:"PlotTimeSeriesBlock",alias:null,template_args:[],ctor_args:[{text:'"Sensor Plot"',span:{start:21320,end:21333}},{text:'{"Measured Position"}',span:{start:21343,end:21364}},{text:"SPS",span:{start:21374,end:21377}},{text:"100.0f",span:{start:21387,end:21393}}],display_name:"Sensor Plot",in_graph:!0,span:{start:21286,end:21400},editable:!0,read_only_reason:null},{var:"error_plot",type_text:"PlotTimeSeriesBlock",type_name:"PlotTimeSeriesBlock",alias:null,template_args:[],ctor_args:[{text:'"Error Plot"',span:{start:21446,end:21458}},{text:'{"target - x"}',span:{start:21468,end:21482}},{text:"SPS",span:{start:21492,end:21495}},{text:"100.0f",span:{start:21505,end:21511}}],display_name:"Error Plot",in_graph:!0,span:{start:21406,end:21518},editable:!0,read_only_reason:null}],runners:[{index:0,block:"controller",block_expr:"&controller",may_block:!1,form:"inline",span:{start:21956,end:22019},editable:!0,read_only_reason:null},{index:1,block:"throttle",block_expr:"&throttle",may_block:!1,form:"inline",span:{start:22025,end:22070},editable:!0,read_only_reason:null},{index:2,block:"plant",block_expr:"&plant",may_block:!1,form:"inline",span:{start:22076,end:22113},editable:!0,read_only_reason:null},{index:3,block:"fanout",block_expr:"&fanout",may_block:!1,form:"inline",span:{start:22119,end:22192},editable:!0,read_only_reason:null},{index:4,block:"plot",block_expr:"&plot",may_block:!1,form:"inline",span:{start:22198,end:22222},editable:!0,read_only_reason:null},{index:5,block:"error_plot",block_expr:"&error_plot",may_block:!1,form:"inline",span:{start:22228,end:22258},editable:!0,read_only_reason:null},{index:6,block:"root_locus",block_expr:"&root_locus",may_block:!1,form:"inline",span:{start:22264,end:22294},editable:!0,read_only_reason:null}],edges:[{from:"controller",to:"throttle",port:{name:"in",index:null,kind:"field"},runner_index:0,arg_index:1,text:"&throttle.in",span:{start:21987,end:21999},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"controller",to:"error_plot",port:{name:"in",index:0,kind:"indexed_field"},runner_index:0,arg_index:2,text:"&error_plot.in[0]",span:{start:22001,end:22018},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"throttle",to:"plant",port:{name:"force_in",index:null,kind:"field"},runner_index:1,arg_index:1,text:"&plant.force_in",span:{start:22054,end:22069},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"plant",to:"fanout",port:{name:"in",index:null,kind:"field"},runner_index:2,arg_index:1,text:"&fanout.in",span:{start:22102,end:22112},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"fanout",to:"plot",port:{name:"in",index:0,kind:"indexed_field"},runner_index:3,arg_index:1,text:"&plot.in[0]",span:{start:22146,end:22157},editable:!0,read_only_reason:null,sample_type:"float",source_type:null,type_conflict:!1},{from:"fanout",to:"controller",port:{name:"measured_position_in",index:null,kind:"field"},runner_index:3,arg_index:2,text:"&controller.measured_position_in",span:{start:22159,end:22191},editable:!0,read_only_reason:null,sample_type:"float",source_type:null,type_conflict:!1}],config:{var:null,source:"absent",assignments:[],run_call_span:{start:22307,end:22322},editable:!0,read_only_reason:null},gui:{var:"gui",span:{start:22329,end:22395},legacy:!1},unresolved:[],editable:!0,read_only_reason:null}],E={sha256:P,version:C,file:R,has_errors:I,errors:z,sites:T},D="c8f60ba7cb1ecb0b3c30baef3e23d23749ca9209e316a7ceb977920ec62038fe",A="0.3.0",M="desktop_examples/plots.cpp",W=!1,G=[],F=JSON.parse('[{"function":"main","call_offset":2373,"span":{"start":2373,"end":3271},"flowgraph_var":"flowgraph","blocks":[{"var":"cw_source","type_text":"SourceCWBlock<std::complex<float>>","type_name":"SourceCWBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":711,"end":730}}],"ctor_args":[{"text":"\\"CWSource\\"","span":{"start":742,"end":752}},{"text":"1.0f","span":{"start":754,"end":758}},{"text":"2.0f","span":{"start":760,"end":764}},{"text":"SPS","span":{"start":766,"end":769}}],"display_name":"CWSource","in_graph":true,"span":{"start":697,"end":771},"editable":true,"read_only_reason":null},{"var":"cw_throttle","type_text":"ThrottleBlock<std::complex<float>>","type_name":"ThrottleBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":790,"end":809}}],"ctor_args":[{"text":"\\"CWThrottle\\"","span":{"start":823,"end":835}},{"text":"SPS","span":{"start":837,"end":840}}],"display_name":"CWThrottle","in_graph":true,"span":{"start":776,"end":842},"editable":true,"read_only_reason":null},{"var":"cw_fanout","type_text":"FanoutBlock<std::complex<float>>","type_name":"FanoutBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":859,"end":878}}],"ctor_args":[{"text":"\\"CWFanout\\"","span":{"start":890,"end":900}},{"text":"3","span":{"start":902,"end":903}}],"display_name":"CWFanout","in_graph":true,"span":{"start":847,"end":905},"editable":true,"read_only_reason":null},{"var":"cw_complex2realimag","type_text":"ComplexToMagPhaseBlock","type_name":"ComplexToMagPhaseBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CWComplex2RealImag\\"","span":{"start":953,"end":973}},{"text":"ComplexToMagPhaseBlock::Mode::RealImag","span":{"start":975,"end":1013}}],"display_name":"CWComplex2RealImag","in_graph":true,"span":{"start":910,"end":1015},"editable":true,"read_only_reason":null},{"var":"cw_timeseries_plot","type_text":"PlotTimeSeriesBlock","type_name":"PlotTimeSeriesBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW-TimeSeriesPlot\\"","span":{"start":1068,"end":1087}},{"text":"{\\"Real\\", \\"Imaginary\\"}","span":{"start":1097,"end":1118}},{"text":"SPS","span":{"start":1128,"end":1131}},{"text":"10.0f","span":{"start":1141,"end":1146}}],"display_name":"CW-TimeSeriesPlot","in_graph":true,"span":{"start":1020,"end":1175},"editable":true,"read_only_reason":null},{"var":"chirp_source","type_text":"SourceChirpBlock<std::complex<float>>","type_name":"SourceChirpBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":1198,"end":1217}}],"ctor_args":[{"text":"\\"ChirpSource\\"","span":{"start":1232,"end":1245}},{"text":"1.0f","span":{"start":1247,"end":1251}},{"text":"200.0f","span":{"start":1253,"end":1259}},{"text":"800.0f","span":{"start":1261,"end":1267}},{"text":"SPS","span":{"start":1269,"end":1272}},{"text":"4.0f","span":{"start":1274,"end":1278}}],"display_name":"ChirpSource","in_graph":true,"span":{"start":1181,"end":1280},"editable":true,"read_only_reason":null},{"var":"chirp_throttle","type_text":"ThrottleBlock<std::complex<float>>","type_name":"ThrottleBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":1299,"end":1318}}],"ctor_args":[{"text":"\\"ChirpThrottle\\"","span":{"start":1335,"end":1350}},{"text":"SPS","span":{"start":1352,"end":1355}}],"display_name":"ChirpThrottle","in_graph":true,"span":{"start":1285,"end":1357},"editable":true,"read_only_reason":null},{"var":"chirp_fanout","type_text":"FanoutBlock<std::complex<float>>","type_name":"FanoutBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":1374,"end":1393}}],"ctor_args":[{"text":"\\"ChirpFanout\\"","span":{"start":1408,"end":1421}},{"text":"3","span":{"start":1423,"end":1424}}],"display_name":"ChirpFanout","in_graph":true,"span":{"start":1362,"end":1426},"editable":true,"read_only_reason":null},{"var":"chirp_c2realimag","type_text":"ComplexToMagPhaseBlock","type_name":"ComplexToMagPhaseBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"ChirpComplex2RealImag\\"","span":{"start":1471,"end":1494}},{"text":"ComplexToMagPhaseBlock::Mode::RealImag","span":{"start":1496,"end":1534}}],"display_name":"ChirpComplex2RealImag","in_graph":true,"span":{"start":1431,"end":1536},"editable":true,"read_only_reason":null},{"var":"chirp_timeseries_plot","type_text":"PlotTimeSeriesBlock","type_name":"PlotTimeSeriesBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"Chirp-TimeSeriesPlot\\"","span":{"start":1592,"end":1614}},{"text":"{\\"Real\\", \\"Imaginary\\"}","span":{"start":1624,"end":1645}},{"text":"SPS","span":{"start":1655,"end":1658}},{"text":"10.0f","span":{"start":1668,"end":1673}}],"display_name":"Chirp-TimeSeriesPlot","in_graph":true,"span":{"start":1541,"end":1702},"editable":true,"read_only_reason":null},{"var":"cspectrum_plot","type_text":"PlotCSpectrumBlock","type_name":"PlotCSpectrumBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"Chirp-CSpectrumPlot\\"","span":{"start":1755,"end":1776}},{"text":"{\\"CW\\", \\"Chirp\\"}","span":{"start":1786,"end":1801}},{"text":"SPS","span":{"start":1811,"end":1814}},{"text":"1024","span":{"start":1824,"end":1828}}],"display_name":"Chirp-CSpectrumPlot","in_graph":true,"span":{"start":1712,"end":1858},"editable":true,"read_only_reason":null},{"var":"cspectrogram_plot","type_text":"PlotCSpectrogramBlock","type_name":"PlotCSpectrogramBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW-SpectrogramPlot\\"","span":{"start":1913,"end":1933}},{"text":"{\\"CW\\", \\"Chirp\\"}","span":{"start":1943,"end":1958}},{"text":"SPS","span":{"start":1968,"end":1971}},{"text":"256","span":{"start":1981,"end":1984}},{"text":"100","span":{"start":2017,"end":2020}}],"display_name":"CW-SpectrogramPlot","in_graph":true,"span":{"start":1864,"end":2035},"editable":true,"read_only_reason":null}],"runners":[{"index":0,"block":"cw_source","block_expr":"&cw_source","may_block":false,"form":"inline","span":{"start":2411,"end":2457},"editable":true,"read_only_reason":null},{"index":1,"block":"cw_throttle","block_expr":"&cw_throttle","may_block":false,"form":"inline","span":{"start":2467,"end":2513},"editable":true,"read_only_reason":null},{"index":2,"block":"cw_fanout","block_expr":"&cw_fanout","may_block":false,"form":"inline","span":{"start":2523,"end":2626},"editable":true,"read_only_reason":null},{"index":3,"block":"cw_complex2realimag","block_expr":"&cw_complex2realimag","may_block":false,"form":"inline","span":{"start":2636,"end":2729},"editable":true,"read_only_reason":null},{"index":4,"block":"cw_timeseries_plot","block_expr":"&cw_timeseries_plot","may_block":false,"form":"inline","span":{"start":2739,"end":2777},"editable":true,"read_only_reason":null},{"index":5,"block":"chirp_source","block_expr":"&chirp_source","may_block":false,"form":"inline","span":{"start":2789,"end":2841},"editable":true,"read_only_reason":null},{"index":6,"block":"chirp_throttle","block_expr":"&chirp_throttle","may_block":false,"form":"inline","span":{"start":2851,"end":2903},"editable":true,"read_only_reason":null},{"index":7,"block":"chirp_fanout","block_expr":"&chirp_fanout","may_block":false,"form":"inline","span":{"start":2913,"end":3016},"editable":true,"read_only_reason":null},{"index":8,"block":"chirp_c2realimag","block_expr":"&chirp_c2realimag","may_block":false,"form":"inline","span":{"start":3026,"end":3122},"editable":true,"read_only_reason":null},{"index":9,"block":"chirp_timeseries_plot","block_expr":"&chirp_timeseries_plot","may_block":false,"form":"inline","span":{"start":3132,"end":3173},"editable":true,"read_only_reason":null},{"index":10,"block":"cspectrum_plot","block_expr":"&cspectrum_plot","may_block":false,"form":"inline","span":{"start":3184,"end":3218},"editable":true,"read_only_reason":null},{"index":11,"block":"cspectrogram_plot","block_expr":"&cspectrogram_plot","may_block":false,"form":"inline","span":{"start":3228,"end":3265},"editable":true,"read_only_reason":null}],"edges":[{"from":"cw_source","to":"cw_throttle","port":{"name":"in","index":null,"kind":"field"},"runner_index":0,"arg_index":1,"text":"&cw_throttle.in","span":{"start":2441,"end":2456},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_throttle","to":"cw_fanout","port":{"name":"in","index":null,"kind":"field"},"runner_index":1,"arg_index":1,"text":"&cw_fanout.in","span":{"start":2499,"end":2512},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_fanout","to":"cw_complex2realimag","port":{"name":"in","index":null,"kind":"field"},"runner_index":2,"arg_index":1,"text":"&cw_complex2realimag.in","span":{"start":2553,"end":2576},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"cw_fanout","to":"cspectrum_plot","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":2,"arg_index":2,"text":"&cspectrum_plot.in[0]","span":{"start":2578,"end":2599},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"cw_fanout","to":"cspectrogram_plot","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":2,"arg_index":3,"text":"&cspectrogram_plot.in[0]","span":{"start":2601,"end":2625},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"cw_complex2realimag","to":"cw_timeseries_plot","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":3,"arg_index":1,"text":"&cw_timeseries_plot.in[0]","span":{"start":2676,"end":2701},"editable":true,"read_only_reason":null,"sample_type":"float","source_type":"float","type_conflict":false},{"from":"cw_complex2realimag","to":"cw_timeseries_plot","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":3,"arg_index":2,"text":"&cw_timeseries_plot.in[1]","span":{"start":2703,"end":2728},"editable":true,"read_only_reason":null,"sample_type":"float","source_type":"float","type_conflict":false},{"from":"chirp_source","to":"chirp_throttle","port":{"name":"in","index":null,"kind":"field"},"runner_index":5,"arg_index":1,"text":"&chirp_throttle.in","span":{"start":2822,"end":2840},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"chirp_throttle","to":"chirp_fanout","port":{"name":"in","index":null,"kind":"field"},"runner_index":6,"arg_index":1,"text":"&chirp_fanout.in","span":{"start":2886,"end":2902},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"chirp_fanout","to":"chirp_c2realimag","port":{"name":"in","index":null,"kind":"field"},"runner_index":7,"arg_index":1,"text":"&chirp_c2realimag.in","span":{"start":2946,"end":2966},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"chirp_fanout","to":"cspectrum_plot","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":7,"arg_index":2,"text":"&cspectrum_plot.in[1]","span":{"start":2968,"end":2989},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"chirp_fanout","to":"cspectrogram_plot","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":7,"arg_index":3,"text":"&cspectrogram_plot.in[1]","span":{"start":2991,"end":3015},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"chirp_c2realimag","to":"chirp_timeseries_plot","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":8,"arg_index":1,"text":"&chirp_timeseries_plot.in[0]","span":{"start":3063,"end":3091},"editable":true,"read_only_reason":null,"sample_type":"float","source_type":"float","type_conflict":false},{"from":"chirp_c2realimag","to":"chirp_timeseries_plot","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":8,"arg_index":2,"text":"&chirp_timeseries_plot.in[1]","span":{"start":3093,"end":3121},"editable":true,"read_only_reason":null,"sample_type":"float","source_type":"float","type_conflict":false}],"config":{"var":null,"source":"absent","assignments":[],"run_call_span":{"start":3278,"end":3293},"editable":true,"read_only_reason":null},"gui":{"var":"gui","span":{"start":3299,"end":3365},"legacy":false},"unresolved":[],"editable":true,"read_only_reason":null}]'),N={sha256:D,version:A,file:M,has_errors:W,errors:G,sites:F},U="b824d6667982e17e4976d5e4867e3595012250287cedfa3f2ca034ed242b74de",H="0.3.0",L="desktop_examples/polyphase_channelizer.cpp",q=!1,O=[],V=JSON.parse('[{"function":"main","call_offset":3944,"span":{"start":3944,"end":4880},"flowgraph_var":"flowgraph","blocks":[{"var":"cw_source0","type_text":"CustomSourceBlock","type_name":"CustomSourceBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW Source 0\\"","span":{"start":2826,"end":2839}},{"text":"1.0f","span":{"start":2841,"end":2845}},{"text":"0.01f","span":{"start":2847,"end":2852}},{"text":"ch0_freq","span":{"start":2854,"end":2862}},{"text":"SPS","span":{"start":2864,"end":2867}}],"display_name":"CW Source 0","in_graph":true,"span":{"start":2797,"end":2869},"editable":true,"read_only_reason":null},{"var":"cw_source1","type_text":"CustomSourceBlock","type_name":"CustomSourceBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW Source 1\\"","span":{"start":2903,"end":2916}},{"text":"3.0f","span":{"start":2918,"end":2922}},{"text":"0.01f","span":{"start":2924,"end":2929}},{"text":"ch1_freq","span":{"start":2931,"end":2939}},{"text":"SPS","span":{"start":2941,"end":2944}}],"display_name":"CW Source 1","in_graph":true,"span":{"start":2874,"end":2946},"editable":true,"read_only_reason":null},{"var":"cw_source2","type_text":"CustomSourceBlock","type_name":"CustomSourceBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW Source 2\\"","span":{"start":2980,"end":2993}},{"text":"10.0f","span":{"start":2995,"end":3000}},{"text":"0.01f","span":{"start":3002,"end":3007}},{"text":"ch2_freq","span":{"start":3009,"end":3017}},{"text":"SPS","span":{"start":3019,"end":3022}}],"display_name":"CW Source 2","in_graph":true,"span":{"start":2951,"end":3024},"editable":true,"read_only_reason":null},{"var":"cw_source3","type_text":"CustomSourceBlock","type_name":"CustomSourceBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW Source 3\\"","span":{"start":3058,"end":3071}},{"text":"30.0f","span":{"start":3073,"end":3078}},{"text":"0.01f","span":{"start":3080,"end":3085}},{"text":"ch3_freq","span":{"start":3087,"end":3095}},{"text":"SPS","span":{"start":3097,"end":3100}}],"display_name":"CW Source 3","in_graph":true,"span":{"start":3029,"end":3102},"editable":true,"read_only_reason":null},{"var":"cw_source4","type_text":"CustomSourceBlock","type_name":"CustomSourceBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"CW Source 4\\"","span":{"start":3136,"end":3149}},{"text":"100.0f","span":{"start":3151,"end":3157}},{"text":"0.01f","span":{"start":3159,"end":3164}},{"text":"ch4_freq","span":{"start":3166,"end":3174}},{"text":"SPS","span":{"start":3176,"end":3179}}],"display_name":"CW Source 4","in_graph":true,"span":{"start":3107,"end":3181},"editable":true,"read_only_reason":null},{"var":"adder","type_text":"AddBlock<std::complex<float>, NUM_CHANNELS>","type_name":"AddBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":3197,"end":3216}},{"text":"NUM_CHANNELS","resolved":"5","span":{"start":3218,"end":3230}}],"ctor_args":[{"text":"\\"Adder\\"","span":{"start":3238,"end":3245}}],"display_name":"Adder","in_graph":true,"span":{"start":3188,"end":3247},"editable":true,"read_only_reason":null},{"var":"throughput","type_text":"ThroughputBlock<std::complex<float>>","type_name":"ThroughputBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":3269,"end":3288}}],"ctor_args":[{"text":"\\"Throughput\\"","span":{"start":3301,"end":3313}}],"display_name":"Throughput","in_graph":true,"span":{"start":3253,"end":3315},"editable":true,"read_only_reason":null},{"var":"channelizer","type_text":"PolyphaseChannelizerBlock<NUM_CHANNELS, 3>","type_name":"PolyphaseChannelizerBlock","alias":null,"template_args":[{"text":"NUM_CHANNELS","resolved":"5","span":{"start":3347,"end":3359}},{"text":"3","resolved":"3","span":{"start":3361,"end":3362}}],"ctor_args":[{"text":"\\"Polyphase Channelizer\\"","span":{"start":3385,"end":3408}},{"text":"80.0f","span":{"start":3418,"end":3423}}],"display_name":"Polyphase Channelizer","in_graph":true,"span":{"start":3321,"end":3452},"editable":true,"read_only_reason":null},{"var":"plot_polyphase_cspectrum","type_text":"PlotCSpectrumBlock","type_name":"PlotCSpectrumBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"Plot Channelizer Spectrum\\"","span":{"start":3511,"end":3538}},{"text":"{\\"pfch 0\\", \\"pfch 1\\", \\"pfch 2\\", \\"pfch 3\\", \\"pfch 4\\"}","span":{"start":3548,"end":3598}},{"text":"static_cast<size_t>(channel_BW)","span":{"start":3608,"end":3639}},{"text":"1024","span":{"start":3649,"end":3653}},{"text":"SpectralWindow::BlackmanHarris","span":{"start":3663,"end":3693}}],"display_name":"Plot Channelizer Spectrum","in_graph":true,"span":{"start":3458,"end":3700},"editable":true,"read_only_reason":null},{"var":"plot_input_cspectrum","type_text":"PlotCSpectrumBlock","type_name":"PlotCSpectrumBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"Plot Input Spectrum\\"","span":{"start":3755,"end":3776}},{"text":"{\\"source 0\\", \\"source 1\\", \\"source 2\\", \\"source 3\\", \\"source 4\\"}","span":{"start":3786,"end":3846}},{"text":"SPS","span":{"start":3856,"end":3859}},{"text":"1024","span":{"start":3869,"end":3873}},{"text":"SpectralWindow::BlackmanHarris","span":{"start":3883,"end":3913}}],"display_name":"Plot Input Spectrum","in_graph":true,"span":{"start":3706,"end":3920},"editable":true,"read_only_reason":null}],"runners":[{"index":0,"block":"cw_source0","block_expr":"&cw_source0","may_block":false,"form":"inline","span":{"start":3982,"end":4055},"editable":true,"read_only_reason":null},{"index":1,"block":"cw_source1","block_expr":"&cw_source1","may_block":false,"form":"inline","span":{"start":4065,"end":4138},"editable":true,"read_only_reason":null},{"index":2,"block":"cw_source2","block_expr":"&cw_source2","may_block":false,"form":"inline","span":{"start":4148,"end":4221},"editable":true,"read_only_reason":null},{"index":3,"block":"cw_source3","block_expr":"&cw_source3","may_block":false,"form":"inline","span":{"start":4231,"end":4304},"editable":true,"read_only_reason":null},{"index":4,"block":"cw_source4","block_expr":"&cw_source4","may_block":false,"form":"inline","span":{"start":4314,"end":4387},"editable":true,"read_only_reason":null},{"index":5,"block":"adder","block_expr":"&adder","may_block":false,"form":"inline","span":{"start":4397,"end":4438},"editable":true,"read_only_reason":null},{"index":6,"block":"throughput","block_expr":"&throughput","may_block":false,"form":"inline","span":{"start":4448,"end":4495},"editable":true,"read_only_reason":null},{"index":7,"block":"channelizer","block_expr":"&channelizer","may_block":false,"form":"inline","span":{"start":4505,"end":4770},"editable":true,"read_only_reason":null},{"index":8,"block":"plot_polyphase_cspectrum","block_expr":"&plot_polyphase_cspectrum","may_block":false,"form":"inline","span":{"start":4780,"end":4824},"editable":true,"read_only_reason":null},{"index":9,"block":"plot_input_cspectrum","block_expr":"&plot_input_cspectrum","may_block":false,"form":"inline","span":{"start":4834,"end":4874},"editable":true,"read_only_reason":null}],"edges":[{"from":"cw_source0","to":"adder","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":0,"arg_index":1,"text":"&adder.in[0]","span":{"start":4013,"end":4025},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source0","to":"plot_input_cspectrum","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":0,"arg_index":2,"text":"&plot_input_cspectrum.in[0]","span":{"start":4027,"end":4054},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source1","to":"adder","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":1,"arg_index":1,"text":"&adder.in[1]","span":{"start":4096,"end":4108},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source1","to":"plot_input_cspectrum","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":1,"arg_index":2,"text":"&plot_input_cspectrum.in[1]","span":{"start":4110,"end":4137},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source2","to":"adder","port":{"name":"in","index":2,"kind":"indexed_field"},"runner_index":2,"arg_index":1,"text":"&adder.in[2]","span":{"start":4179,"end":4191},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source2","to":"plot_input_cspectrum","port":{"name":"in","index":2,"kind":"indexed_field"},"runner_index":2,"arg_index":2,"text":"&plot_input_cspectrum.in[2]","span":{"start":4193,"end":4220},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source3","to":"adder","port":{"name":"in","index":3,"kind":"indexed_field"},"runner_index":3,"arg_index":1,"text":"&adder.in[3]","span":{"start":4262,"end":4274},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source3","to":"plot_input_cspectrum","port":{"name":"in","index":3,"kind":"indexed_field"},"runner_index":3,"arg_index":2,"text":"&plot_input_cspectrum.in[3]","span":{"start":4276,"end":4303},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source4","to":"adder","port":{"name":"in","index":4,"kind":"indexed_field"},"runner_index":4,"arg_index":1,"text":"&adder.in[4]","span":{"start":4345,"end":4357},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"cw_source4","to":"plot_input_cspectrum","port":{"name":"in","index":4,"kind":"indexed_field"},"runner_index":4,"arg_index":2,"text":"&plot_input_cspectrum.in[4]","span":{"start":4359,"end":4386},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"adder","to":"throughput","port":{"name":"in","index":null,"kind":"field"},"runner_index":5,"arg_index":1,"text":"&throughput.in","span":{"start":4423,"end":4437},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"throughput","to":"channelizer","port":{"name":"in","index":null,"kind":"field"},"runner_index":6,"arg_index":1,"text":"&channelizer.in","span":{"start":4479,"end":4494},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"channelizer","to":"plot_polyphase_cspectrum","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":7,"arg_index":1,"text":"&plot_polyphase_cspectrum.in[0]","span":{"start":4549,"end":4580},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"channelizer","to":"plot_polyphase_cspectrum","port":{"name":"in","index":1,"kind":"indexed_field"},"runner_index":7,"arg_index":2,"text":"&plot_polyphase_cspectrum.in[1]","span":{"start":4594,"end":4625},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"channelizer","to":"plot_polyphase_cspectrum","port":{"name":"in","index":2,"kind":"indexed_field"},"runner_index":7,"arg_index":3,"text":"&plot_polyphase_cspectrum.in[2]","span":{"start":4639,"end":4670},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"channelizer","to":"plot_polyphase_cspectrum","port":{"name":"in","index":3,"kind":"indexed_field"},"runner_index":7,"arg_index":4,"text":"&plot_polyphase_cspectrum.in[3]","span":{"start":4684,"end":4715},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"channelizer","to":"plot_polyphase_cspectrum","port":{"name":"in","index":4,"kind":"indexed_field"},"runner_index":7,"arg_index":5,"text":"&plot_polyphase_cspectrum.in[4]","span":{"start":4729,"end":4760},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false}],"config":{"var":"config","source":"variable","assignments":[{"path":"scheduler","value":"cler::SchedulerType::FixedThreadPool","span":{"start":4921,"end":4976},"value_span":{"start":4940,"end":4976},"editable":true,"read_only_reason":null},{"path":"collect_detailed_stats","value":"true","span":{"start":4982,"end":5018},"value_span":{"start":5014,"end":5018},"editable":true,"read_only_reason":null}],"run_call_span":{"start":5024,"end":5045},"editable":true,"read_only_reason":null},"gui":{"var":"gui_manager","span":{"start":5335,"end":5417},"legacy":false},"unresolved":[],"editable":true,"read_only_reason":null}]'),$={sha256:U,version:H,file:L,has_errors:q,errors:O,sites:V},Q="611d66230c749ae1efd30127a473b677871dfa82799900abcc9c591486e57186",X="0.3.0",K="desktop_examples/spike/spike.cpp",Z=!1,j=[],Y=[{function:"run_app",call_offset:4778,span:{start:4778,end:5278},flowgraph_var:"flowgraph",blocks:[{var:"source",type_text:"SpikeSourceBlock",type_name:"SpikeSourceBlock",alias:null,template_args:[],ctor_args:[{text:'"Source"',span:{start:1362,end:1370}},{text:"args.source",span:{start:1372,end:1383}},{text:"args.freq",span:{start:1385,end:1394}},{text:"args.rate",span:{start:1396,end:1405}},{text:"args.device_address",span:{start:1435,end:1454}},{text:"args.gain",span:{start:1456,end:1465}},{text:"args.lna",span:{start:1495,end:1503}},{text:"args.vga",span:{start:1505,end:1513}},{text:"args.amp",span:{start:1515,end:1523}}],display_name:"Source",in_graph:!0,span:{start:1338,end:1525},editable:!0,read_only_reason:null},{var:"fanout",type_text:"FanoutBlock<std::complex<float>>",type_name:"FanoutBlock",alias:null,template_args:[{text:"std::complex<float>",resolved:null,span:{start:1618,end:1637}}],ctor_args:[{text:'"Fanout"',span:{start:1646,end:1654}},{text:"4",span:{start:1656,end:1657}}],display_name:"Fanout",in_graph:!0,span:{start:1606,end:1659},editable:!0,read_only_reason:null},{var:"power",type_text:"PowerDetectorBlock<std::complex<float>>",type_name:"PowerDetectorBlock",alias:null,template_args:[{text:"std::complex<float>",resolved:null,span:{start:1683,end:1702}}],ctor_args:[{text:'"PowerDetector"',span:{start:1710,end:1725}},{text:"-120.0f",span:{start:1727,end:1734}}],display_name:"PowerDetector",in_graph:!0,span:{start:1664,end:1736},editable:!0,read_only_reason:null},{var:"trigger",type_text:"Trig",type_name:"TriggerBlock",alias:"Trig",template_args:[{text:"float",resolved:null,span:{start:602,end:607}}],ctor_args:[{text:'"Trigger"',span:{start:1755,end:1764}},{text:"static_cast<size_t>(args.rate)",span:{start:1766,end:1796}},{text:"-40.0f",span:{start:1831,end:1837}},{text:"20.0f",span:{start:1873,end:1878}},{text:"10.0f",span:{start:1914,end:1919}},{text:"100.0f",span:{start:1955,end:1961}},{text:"Trig::Edge::Rising",span:{start:1980,end:1998}},{text:"Trig::Mode::Auto",span:{start:2017,end:2033}},{text:"3.0f",span:{start:2069,end:2073}},{text:"200.0f",span:{start:2109,end:2115}},{text:"5000.0f",span:{start:2151,end:2158}}],display_name:"Trigger",in_graph:!0,span:{start:1742,end:2160},editable:!0,read_only_reason:null},{var:"spectrum",type_text:"PlotCSpectrumBlock",type_name:"PlotCSpectrumBlock",alias:null,template_args:[],ctor_args:[{text:'"Spectrum"',span:{start:2194,end:2204}},{text:'{"I/Q"}',span:{start:2206,end:2213}},{text:"static_cast<size_t>(args.rate)",span:{start:2215,end:2245}},{text:"args.fft",span:{start:2247,end:2255}}],display_name:"Spectrum",in_graph:!0,span:{start:2166,end:2257},editable:!0,read_only_reason:null},{var:"spectrogram",type_text:"PlotCSpectrogramBlock",type_name:"PlotCSpectrogramBlock",alias:null,template_args:[],ctor_args:[{text:'"Spectrogram"',span:{start:3212,end:3225}},{text:'{"I/Q"}',span:{start:3227,end:3234}},{text:"static_cast<size_t>(args.rate)",span:{start:3244,end:3274}},{text:"args.fft",span:{start:3276,end:3284}},{text:"waterfall_rows",span:{start:3286,end:3300}}],display_name:"Spectrogram",in_graph:!0,span:{start:3178,end:3302},editable:!0,read_only_reason:null},{var:"channelizer",type_text:"ChannelizerPanelBlock",type_name:"ChannelizerPanelBlock",alias:null,template_args:[],ctor_args:[{text:'"Channelizer"',span:{start:3342,end:3355}}],display_name:"Channelizer",in_graph:!0,span:{start:3308,end:3357},editable:!0,read_only_reason:null},{var:"panel",type_text:"ControlPanel",type_name:"ControlPanel",alias:null,template_args:[],ctor_args:[{text:'"ControlPanel"',span:{start:3651,end:3665}},{text:"&src_if",span:{start:3667,end:3674}},{text:"&trigger",span:{start:3676,end:3684}},{text:"&spectrum",span:{start:3686,end:3695}},{text:"&spectrogram",span:{start:3697,end:3709}},{text:"&power",span:{start:3734,end:3740}},{text:"&channelizer",span:{start:3742,end:3754}},{text:"waterfall_rows",span:{start:3756,end:3770}},{text:"args",span:{start:3772,end:3776}}],display_name:"ControlPanel",in_graph:!0,span:{start:3632,end:3778},editable:!0,read_only_reason:null},{var:"capture",type_text:"CaptureBlock",type_name:"CaptureBlock",alias:null,template_args:[],ctor_args:[{text:'"Capture"',span:{start:4569,end:4578}},{text:"args",span:{start:4580,end:4584}},{text:"&panel",span:{start:4586,end:4592}},{text:"&trigger",span:{start:4594,end:4602}},{text:"&spectrum",span:{start:4604,end:4613}},{text:"&spectrogram",span:{start:4640,end:4652}},{text:"&gui",span:{start:4654,end:4658}}],display_name:"Capture",in_graph:!0,span:{start:4548,end:4660},editable:!0,read_only_reason:null}],runners:[{index:0,block:"panel",block_expr:"&panel",may_block:!1,form:"inline",span:{start:4816,end:4841},editable:!0,read_only_reason:null},{index:1,block:"source",block_expr:"&source",may_block:!1,form:"inline",span:{start:4851,end:4890},editable:!0,read_only_reason:null},{index:2,block:"fanout",block_expr:"&fanout",may_block:!1,form:"inline",span:{start:4900,end:5028},editable:!0,read_only_reason:null},{index:3,block:"power",block_expr:"&power",may_block:!1,form:"inline",span:{start:5038,end:5078},editable:!0,read_only_reason:null},{index:4,block:"trigger",block_expr:"&trigger",may_block:!1,form:"inline",span:{start:5088,end:5115},editable:!0,read_only_reason:null},{index:5,block:"spectrum",block_expr:"&spectrum",may_block:!1,form:"inline",span:{start:5125,end:5153},editable:!0,read_only_reason:null},{index:6,block:"spectrogram",block_expr:"&spectrogram",may_block:!1,form:"inline",span:{start:5163,end:5194},editable:!0,read_only_reason:null},{index:7,block:"channelizer",block_expr:"&channelizer",may_block:!1,form:"inline",span:{start:5204,end:5235},editable:!0,read_only_reason:null},{index:8,block:"capture",block_expr:"&capture",may_block:!1,form:"inline",span:{start:5245,end:5272},editable:!0,read_only_reason:null}],edges:[{from:"source",to:"fanout",port:{name:"in",index:null,kind:"field"},runner_index:1,arg_index:1,text:"&fanout.in",span:{start:4879,end:4889},editable:!0,read_only_reason:null,sample_type:"std::complex<float>",source_type:null,type_conflict:!1},{from:"fanout",to:"power",port:{name:"in",index:null,kind:"field"},runner_index:2,arg_index:1,text:"&power.in",span:{start:4928,end:4937},editable:!0,read_only_reason:null,sample_type:null,source_type:null,type_conflict:!1},{from:"fanout",to:"spectrum",port:{name:"in",index:0,kind:"indexed_field"},runner_index:2,arg_index:2,text:"&spectrum.in[0]",span:{start:4939,end:4954},editable:!0,read_only_reason:null,sample_type:"std::complex<float>",source_type:null,type_conflict:!1},{from:"fanout",to:"spectrogram",port:{name:"in",index:0,kind:"indexed_field"},runner_index:2,arg_index:3,text:"&spectrogram.in[0]",span:{start:4956,end:4974},editable:!0,read_only_reason:null,sample_type:"std::complex<float>",source_type:null,type_conflict:!1},{from:"fanout",to:"channelizer",port:{name:"in",index:null,kind:"field"},runner_index:2,arg_index:4,text:"&channelizer.in",span:{start:5012,end:5027},editable:!0,read_only_reason:null,sample_type:null,source_type:null,type_conflict:!1},{from:"power",to:"trigger",port:{name:"in",index:null,kind:"field"},runner_index:3,arg_index:1,text:"&trigger.in",span:{start:5066,end:5077},editable:!0,read_only_reason:null,sample_type:"float",source_type:null,type_conflict:!1}],config:{var:null,source:"absent",assignments:[],run_call_span:{start:5285,end:5300},editable:!0,read_only_reason:null},gui:{var:"gui",span:{start:5619,end:5685},legacy:!1},unresolved:[],editable:!0,read_only_reason:null}],J={sha256:Q,version:X,file:K,has_errors:Z,errors:j,sites:Y},ee="072f17e4e3a1e76b41b980200656a61ab7472f700cb588b1534be13a14b8e1bb",te="0.3.0",ne="tools/cler-fg/cler-graph/tests/data/type_conflict.cpp",re=!1,ae=[],le=[{function:"main",call_offset:1097,span:{start:1097,end:1317},flowgraph_var:"flowgraph",blocks:[{var:"ramp",type_text:"RampSourceBlock",type_name:"RampSourceBlock",alias:null,template_args:[],ctor_args:[{text:'"Ramp Source"',span:{start:896,end:909}}],display_name:"Ramp Source",in_graph:!0,span:{start:875,end:911},editable:!0,read_only_reason:null},{var:"throttle",type_text:"ThrottleBlock<float>",type_name:"ThrottleBlock",alias:null,template_args:[{text:"float",resolved:null,span:{start:930,end:935}}],ctor_args:[{text:'"Throttle"',span:{start:946,end:956}},{text:"1000",span:{start:958,end:962}}],display_name:"Throttle",in_graph:!0,span:{start:916,end:964},editable:!0,read_only_reason:null},{var:"throughput",type_text:"ThroughputBlock<std::complex<float>>",type_name:"ThroughputBlock",alias:null,template_args:[{text:"std::complex<float>",resolved:null,span:{start:985,end:1004}}],ctor_args:[{text:'"Throughput"',span:{start:1017,end:1029}}],display_name:"Throughput",in_graph:!0,span:{start:969,end:1031},editable:!0,read_only_reason:null},{var:"sink",type_text:"ComplexSinkBlock",type_name:"ComplexSinkBlock",alias:null,template_args:[],ctor_args:[{text:'"Complex Sink"',span:{start:1058,end:1072}}],display_name:"Complex Sink",in_graph:!0,span:{start:1036,end:1074},editable:!0,read_only_reason:null}],runners:[{index:0,block:"ramp",block_expr:"&ramp",may_block:!1,form:"inline",span:{start:1135,end:1173},editable:!0,read_only_reason:null},{index:1,block:"throttle",block_expr:"&throttle",may_block:!1,form:"inline",span:{start:1183,end:1227},editable:!0,read_only_reason:null},{index:2,block:"throughput",block_expr:"&throughput",may_block:!1,form:"inline",span:{start:1237,end:1277},editable:!0,read_only_reason:null},{index:3,block:"sink",block_expr:"&sink",may_block:!1,form:"inline",span:{start:1287,end:1311},editable:!0,read_only_reason:null}],edges:[{from:"ramp",to:"throttle",port:{name:"in",index:null,kind:"field"},runner_index:0,arg_index:1,text:"&throttle.in",span:{start:1160,end:1172},editable:!0,read_only_reason:null,sample_type:"float",source_type:"float",type_conflict:!1},{from:"throttle",to:"throughput",port:{name:"in",index:null,kind:"field"},runner_index:1,arg_index:1,text:"&throughput.in",span:{start:1212,end:1226},editable:!0,read_only_reason:null,sample_type:"std::complex<float>",source_type:"float",type_conflict:!0},{from:"throughput",to:"sink",port:{name:"in",index:null,kind:"field"},runner_index:2,arg_index:1,text:"&sink.in",span:{start:1268,end:1276},editable:!0,read_only_reason:null,sample_type:"std::complex<float>",source_type:"std::complex<float>",type_conflict:!1}],config:{var:null,source:"absent",assignments:[],run_call_span:{start:1324,end:1339},editable:!0,read_only_reason:null},gui:null,unresolved:[],editable:!0,read_only_reason:null}],oe={sha256:ee,version:te,file:ne,has_errors:re,errors:ae,sites:le},se="5888389e5cb059fee3d823f97119d781b69edecc459d4278d861f61afd090cd3",ie="0.3.0",ce="desktop_examples/uhd_device.cpp",pe=!1,de=[],_e=JSON.parse('[{"function":"mode_rx","call_offset":5840,"span":{"start":5840,"end":6079},"flowgraph_var":"flowgraph","blocks":[{"var":"usrp_source","type_text":"SourceUHDBlock<std::complex<float>>","type_name":"SourceUHDBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":5327,"end":5346}}],"ctor_args":[{"text":"\\"USRP\\"","span":{"start":5360,"end":5366}},{"text":"args.freq","span":{"start":5368,"end":5377}},{"text":"args.rate","span":{"start":5387,"end":5396}},{"text":"args.device_address","span":{"start":5398,"end":5417}},{"text":"args.gain","span":{"start":5419,"end":5428}},{"text":"1","span":{"start":5430,"end":5431}}],"display_name":"USRP","in_graph":true,"span":{"start":5312,"end":5450},"editable":true,"read_only_reason":null},{"var":"spectrum","type_text":"PlotCSpectrumBlock","type_name":"PlotCSpectrumBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"USRP Spectrum\\"","span":{"start":5547,"end":5562}},{"text":"{\\"I/Q\\"}","span":{"start":5564,"end":5571}},{"text":"args.rate","span":{"start":5573,"end":5582}},{"text":"args.fft","span":{"start":5584,"end":5592}}],"display_name":"USRP Spectrum","in_graph":true,"span":{"start":5519,"end":5594},"editable":true,"read_only_reason":null},{"var":"spectrogram","type_text":"PlotCSpectrogramBlock","type_name":"PlotCSpectrogramBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"Spectrogram\\"","span":{"start":5633,"end":5646}},{"text":"{\\"usrp_signal\\"}","span":{"start":5648,"end":5663}},{"text":"args.rate","span":{"start":5665,"end":5674}},{"text":"args.fft","span":{"start":5676,"end":5684}},{"text":"4000","span":{"start":5686,"end":5690}}],"display_name":"Spectrogram","in_graph":true,"span":{"start":5599,"end":5692},"editable":true,"read_only_reason":null},{"var":"fanout","type_text":"FanoutBlock<std::complex<float>>","type_name":"FanoutBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":5709,"end":5728}}],"ctor_args":[{"text":"\\"Fanout\\"","span":{"start":5737,"end":5745}},{"text":"2","span":{"start":5747,"end":5748}}],"display_name":"Fanout","in_graph":true,"span":{"start":5697,"end":5750},"editable":true,"read_only_reason":null}],"runners":[{"index":0,"block":"usrp_source","block_expr":"&usrp_source","may_block":false,"form":"inline","span":{"start":5878,"end":5921},"editable":true,"read_only_reason":null},{"index":1,"block":"fanout","block_expr":"&fanout","may_block":false,"form":"inline","span":{"start":5931,"end":5994},"editable":true,"read_only_reason":null},{"index":2,"block":"spectrum","block_expr":"&spectrum","may_block":false,"form":"inline","span":{"start":6004,"end":6032},"editable":true,"read_only_reason":null},{"index":3,"block":"spectrogram","block_expr":"&spectrogram","may_block":false,"form":"inline","span":{"start":6042,"end":6073},"editable":true,"read_only_reason":null}],"edges":[{"from":"usrp_source","to":"fanout","port":{"name":"in","index":null,"kind":"field"},"runner_index":0,"arg_index":1,"text":"&fanout.in","span":{"start":5910,"end":5920},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"fanout","to":"spectrum","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":1,"text":"&spectrum.in[0]","span":{"start":5958,"end":5973},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"fanout","to":"spectrogram","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":2,"text":"&spectrogram.in[0]","span":{"start":5975,"end":5993},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false}],"config":{"var":null,"source":"absent","assignments":[],"run_call_span":{"start":6086,"end":6101},"editable":true,"read_only_reason":null},"gui":{"var":"gui","span":{"start":6184,"end":6250},"legacy":false},"unresolved":[],"editable":true,"read_only_reason":null},{"function":"mode_tx_chirp","call_offset":7298,"span":{"start":7298,"end":7527},"flowgraph_var":"flowgraph","blocks":[{"var":"usrp_sink","type_text":"SinkUHDBlock<std::complex<float>>","type_name":"SinkUHDBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":6529,"end":6548}}],"ctor_args":[{"text":"\\"USRP_TX\\"","span":{"start":6560,"end":6569}},{"text":"args.device_address","span":{"start":6579,"end":6598}},{"text":"1","span":{"start":6600,"end":6601}},{"text":"0","span":{"start":6620,"end":6621}},{"text":"\\"sc16\\"","span":{"start":6634,"end":6640}},{"text":"&config","span":{"start":6642,"end":6649}}],"display_name":"USRP_TX","in_graph":true,"span":{"start":6516,"end":6651},"editable":true,"read_only_reason":null},{"var":"chirp","type_text":"SourceChirpBlock<std::complex<float>>","type_name":"SourceChirpBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":6737,"end":6756}}],"ctor_args":[{"text":"\\"Chirp\\"","span":{"start":6764,"end":6771}},{"text":"args.amp","span":{"start":6822,"end":6830}},{"text":"-500e3f","span":{"start":6881,"end":6888}},{"text":"500e3f","span":{"start":6939,"end":6945}},{"text":"args.rate","span":{"start":6996,"end":7005}},{"text":"args.chirp_duration_s","span":{"start":7056,"end":7077}}],"display_name":"Chirp","in_graph":true,"span":{"start":6720,"end":7079},"editable":true,"read_only_reason":null},{"var":"fanout","type_text":"FanoutBlock<std::complex<float>>","type_name":"FanoutBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":7096,"end":7115}}],"ctor_args":[{"text":"\\"Fanout\\"","span":{"start":7124,"end":7132}},{"text":"2","span":{"start":7134,"end":7135}}],"display_name":"Fanout","in_graph":true,"span":{"start":7084,"end":7137},"editable":true,"read_only_reason":null},{"var":"spectrum","type_text":"PlotCSpectrumBlock","type_name":"PlotCSpectrumBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"TX Spectrum\\"","span":{"start":7170,"end":7183}},{"text":"{\\"Chirp\\"}","span":{"start":7185,"end":7194}},{"text":"args.rate","span":{"start":7196,"end":7205}},{"text":"2048","span":{"start":7207,"end":7211}}],"display_name":"TX Spectrum","in_graph":true,"span":{"start":7142,"end":7213},"editable":true,"read_only_reason":null}],"runners":[{"index":0,"block":"chirp","block_expr":"&chirp","may_block":false,"form":"inline","span":{"start":7336,"end":7373},"editable":true,"read_only_reason":null},{"index":1,"block":"fanout","block_expr":"&fanout","may_block":false,"form":"inline","span":{"start":7383,"end":7444},"editable":true,"read_only_reason":null},{"index":2,"block":"spectrum","block_expr":"&spectrum","may_block":false,"form":"inline","span":{"start":7454,"end":7482},"editable":true,"read_only_reason":null},{"index":3,"block":"usrp_sink","block_expr":"&usrp_sink","may_block":false,"form":"inline","span":{"start":7492,"end":7521},"editable":true,"read_only_reason":null}],"edges":[{"from":"chirp","to":"fanout","port":{"name":"in","index":null,"kind":"field"},"runner_index":0,"arg_index":1,"text":"&fanout.in","span":{"start":7362,"end":7372},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"fanout","to":"spectrum","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":1,"text":"&spectrum.in[0]","span":{"start":7410,"end":7425},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"fanout","to":"usrp_sink","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":2,"text":"&usrp_sink.in[0]","span":{"start":7427,"end":7443},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false}],"config":{"var":null,"source":"absent","assignments":[],"run_call_span":{"start":7534,"end":7549},"editable":true,"read_only_reason":null},"gui":{"var":"gui","span":{"start":7638,"end":7704},"legacy":false},"unresolved":[],"editable":true,"read_only_reason":null},{"function":"mode_tx_cw","call_offset":8476,"span":{"start":8476,"end":8702},"flowgraph_var":"flowgraph","blocks":[{"var":"usrp_sink","type_text":"SinkUHDBlock<std::complex<float>>","type_name":"SinkUHDBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":7980,"end":7999}}],"ctor_args":[{"text":"\\"USRP_TX\\"","span":{"start":8011,"end":8020}},{"text":"args.device_address","span":{"start":8030,"end":8049}},{"text":"1","span":{"start":8051,"end":8052}},{"text":"0","span":{"start":8071,"end":8072}},{"text":"\\"sc16\\"","span":{"start":8085,"end":8091}},{"text":"&config","span":{"start":8093,"end":8100}}],"display_name":"USRP_TX","in_graph":true,"span":{"start":7967,"end":8102},"editable":true,"read_only_reason":null},{"var":"cw","type_text":"SourceCWBlock<std::complex<float>>","type_name":"SourceCWBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":8188,"end":8207}}],"ctor_args":[{"text":"\\"CW\\"","span":{"start":8212,"end":8216}},{"text":"args.amp","span":{"start":8218,"end":8226}},{"text":"args.cw_offset","span":{"start":8228,"end":8242}},{"text":"args.rate","span":{"start":8244,"end":8253}}],"display_name":"CW","in_graph":true,"span":{"start":8174,"end":8255},"editable":true,"read_only_reason":null},{"var":"fanout","type_text":"FanoutBlock<std::complex<float>>","type_name":"FanoutBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":8272,"end":8291}}],"ctor_args":[{"text":"\\"Fanout\\"","span":{"start":8300,"end":8308}},{"text":"2","span":{"start":8310,"end":8311}}],"display_name":"Fanout","in_graph":true,"span":{"start":8260,"end":8313},"editable":true,"read_only_reason":null},{"var":"spectrum","type_text":"PlotCSpectrumBlock","type_name":"PlotCSpectrumBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"TX Spectrum\\"","span":{"start":8346,"end":8359}},{"text":"{\\"CW Tone\\"}","span":{"start":8361,"end":8372}},{"text":"args.rate","span":{"start":8374,"end":8383}},{"text":"2048","span":{"start":8385,"end":8389}}],"display_name":"TX Spectrum","in_graph":true,"span":{"start":8318,"end":8391},"editable":true,"read_only_reason":null}],"runners":[{"index":0,"block":"cw","block_expr":"&cw","may_block":false,"form":"inline","span":{"start":8514,"end":8548},"editable":true,"read_only_reason":null},{"index":1,"block":"fanout","block_expr":"&fanout","may_block":false,"form":"inline","span":{"start":8558,"end":8619},"editable":true,"read_only_reason":null},{"index":2,"block":"spectrum","block_expr":"&spectrum","may_block":false,"form":"inline","span":{"start":8629,"end":8657},"editable":true,"read_only_reason":null},{"index":3,"block":"usrp_sink","block_expr":"&usrp_sink","may_block":false,"form":"inline","span":{"start":8667,"end":8696},"editable":true,"read_only_reason":null}],"edges":[{"from":"cw","to":"fanout","port":{"name":"in","index":null,"kind":"field"},"runner_index":0,"arg_index":1,"text":"&fanout.in","span":{"start":8537,"end":8547},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":"std::complex<float>","type_conflict":false},{"from":"fanout","to":"spectrum","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":1,"text":"&spectrum.in[0]","span":{"start":8585,"end":8600},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false},{"from":"fanout","to":"usrp_sink","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":2,"text":"&usrp_sink.in[0]","span":{"start":8602,"end":8618},"editable":true,"read_only_reason":null,"sample_type":"std::complex<float>","source_type":null,"type_conflict":false}],"config":{"var":null,"source":"absent","assignments":[],"run_call_span":{"start":8709,"end":8724},"editable":true,"read_only_reason":null},"gui":{"var":"gui","span":{"start":8808,"end":8874},"legacy":false},"unresolved":[],"editable":true,"read_only_reason":null},{"function":"mode_zero_span","call_offset":9676,"span":{"start":9676,"end":9874},"flowgraph_var":"flowgraph","blocks":[{"var":"usrp_source","type_text":"SourceUHDBlock<std::complex<float>>","type_name":"SourceUHDBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":9100,"end":9119}}],"ctor_args":[{"text":"\\"USRP\\"","span":{"start":9133,"end":9139}},{"text":"args.freq","span":{"start":9141,"end":9150}},{"text":"args.rate","span":{"start":9160,"end":9169}},{"text":"args.device_address","span":{"start":9171,"end":9190}},{"text":"args.gain","span":{"start":9192,"end":9201}},{"text":"1","span":{"start":9203,"end":9204}}],"display_name":"USRP","in_graph":true,"span":{"start":9085,"end":9206},"editable":true,"read_only_reason":null},{"var":"power_detector","type_text":"PowerDetectorBlock<std::complex<float>>","type_name":"PowerDetectorBlock","alias":null,"template_args":[{"text":"std::complex<float>","resolved":null,"span":{"start":9304,"end":9323}}],"ctor_args":[{"text":"\\"PowerDetector\\"","span":{"start":9340,"end":9355}},{"text":"-80.0f","span":{"start":9357,"end":9363}}],"display_name":"PowerDetector","in_graph":true,"span":{"start":9285,"end":9365},"editable":true,"read_only_reason":null},{"var":"power_plot","type_text":"PlotTimeSeriesBlock","type_name":"PlotTimeSeriesBlock","alias":null,"template_args":[],"ctor_args":[{"text":"\\"Power vs Time\\"","span":{"start":9402,"end":9417}},{"text":"{\\"Power (dB)\\"}","span":{"start":9454,"end":9468}},{"text":"args.rate","span":{"start":9505,"end":9514}},{"text":"5.0f","span":{"start":9551,"end":9555}}],"display_name":"Power vs Time","in_graph":true,"span":{"start":9371,"end":9557},"editable":true,"read_only_reason":null}],"runners":[{"index":0,"block":"usrp_source","block_expr":"&usrp_source","may_block":false,"form":"inline","span":{"start":9714,"end":9765},"editable":true,"read_only_reason":null},{"index":1,"block":"power_detector","block_expr":"&power_detector","may_block":false,"form":"inline","span":{"start":9775,"end":9828},"editable":true,"read_only_reason":null},{"index":2,"block":"power_plot","block_expr":"&power_plot","may_block":false,"form":"inline","span":{"start":9838,"end":9868},"editable":true,"read_only_reason":null}],"edges":[{"from":"usrp_source","to":"power_detector","port":{"name":"in","index":null,"kind":"field"},"runner_index":0,"arg_index":1,"text":"&power_detector.in","span":{"start":9746,"end":9764},"editable":true,"read_only_reason":null,"sample_type":null,"source_type":null,"type_conflict":false},{"from":"power_detector","to":"power_plot","port":{"name":"in","index":0,"kind":"indexed_field"},"runner_index":1,"arg_index":1,"text":"&power_plot.in[0]","span":{"start":9810,"end":9827},"editable":true,"read_only_reason":null,"sample_type":"float","source_type":null,"type_conflict":false}],"config":{"var":null,"source":"absent","assignments":[],"run_call_span":{"start":9881,"end":9896},"editable":true,"read_only_reason":null},"gui":{"var":"gui","span":{"start":10069,"end":10135},"legacy":false},"unresolved":[],"editable":true,"read_only_reason":null}]'),ue={sha256:se,version:ie,file:ce,has_errors:pe,errors:de,sites:_e},fe=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <optional>
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/adsb/adsb_decoder.hpp"
#include "desktop_blocks/adsb/adsb_aggregate.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include <cmath>

void on_aircraft_update(const ADSBState&, void*) {}

using SoapyTypeCS16 = SourceSoapySDRBlock<std::complex<int16_t>>;
using FileTypeCS16 = SourceFileBlock<std::complex<int16_t>>;
using SourceVariant = std::variant<SoapyTypeCS16, FileTypeCS16>;

inline auto make_source_variant(bool use_soapy, const std::string& device_args_or_filename,
                                uint64_t freq, uint32_t rate, double gain) {
    if (use_soapy) {
        return SourceVariant(std::in_place_type<SoapyTypeCS16>,
                                    "SoapySourceCS16", device_args_or_filename, freq, rate, gain);
    } else {
        return SourceVariant(std::in_place_type<FileTypeCS16>,
                                    "FileSourceCS16", device_args_or_filename.c_str(), false);
    }
}

// Selects a live SoapySDR device or a recorded IQ file at construction, same procedure() either way
struct SelectableSourceBlock : public cler::BlockBase {
    SourceVariant source;

    SelectableSourceBlock(const char* name, bool use_soapy, const std::string& device_args_or_filename,
                         uint64_t freq, uint32_t rate, double gain)
        : cler::BlockBase(name),
          source(make_source_variant(use_soapy, device_args_or_filename, freq, rate, gain))
    {
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<int16_t>>* out) {
        return std::visit([&](auto& src) {
            return src.procedure(out);
        }, source);
    }
};

struct IQToMagnitudeBlock : public cler::BlockBase {
    cler::Channel<std::complex<int16_t>> in;

    // 1 Hz high-pass DC offset removal filter state
    float z1_I = 0.0f;
    float z1_Q = 0.0f;
    float dc_a;
    float dc_b;

    IQToMagnitudeBlock(const char* name, uint32_t sample_rate = 2000000)
        : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<int16_t>)) {
        dc_b = expf(-2.0f * M_PI * 1.0f / sample_rate);
        dc_a = 1.0f - dc_b;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint16_t>* mag_out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = mag_out->write_dbf();
        if (write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_process = std::min(read_size, write_size);

        for (size_t k = 0; k < to_process; k++) {
            float fI = read_ptr[k].real() / 32768.0f;
            float fQ = read_ptr[k].imag() / 32768.0f;

            z1_I = fI * dc_a + z1_I * dc_b;
            z1_Q = fQ * dc_a + z1_Q * dc_b;
            fI -= z1_I;
            fQ -= z1_Q;

            float magsq = fI * fI + fQ * fQ;
            if (magsq > 1.0f) magsq = 1.0f;

            float mag = sqrtf(magsq) * 65535.0f + 0.5f;
            write_ptr[k] = static_cast<uint16_t>(mag);
        }

        in.commit_read(to_process);
        mag_out->commit_write(to_process);

        return cler::Empty{};
    }
};

int main(int argc, char** argv) {
    std::string source_arg;
    float initial_lat = 32.0f;   // Default: Israel
    float initial_lon = 34.0f;
    float sample_rate_mhz = -1.0f;  // No default - must be specified

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--source" && i + 1 < argc) {
            source_arg = argv[++i];
        } else if (arg == "--lat" && i + 1 < argc) {
            initial_lat = std::atof(argv[++i]);
        } else if (arg == "--lon" && i + 1 < argc) {
            initial_lon = std::atof(argv[++i]);
        } else if (arg == "--rate" && i + 1 < argc) {
            sample_rate_mhz = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\\n";
            std::cout << "\\nOptions:\\n";
            std::cout << "  --source <src>  - \\"soapy\\" for SoapySDR device, or path to IQ file (required)\\n";
            std::cout << "  --rate <rate>   - Sample rate in MHz: 2 or 2.4 (required)\\n";
            std::cout << "  --lat <lat>     - Map center latitude (default: 32.0)\\n";
            std::cout << "  --lon <lon>     - Map center longitude (default: 34.0)\\n";
            std::cout << "\\nExamples:\\n";
            std::cout << "  " << argv[0] << " --source soapy --rate 2\\n";
            std::cout << "  " << argv[0] << " --source adsb_recording.bin --rate 2.4\\n";
            std::cout << "  " << argv[0] << " --source soapy --rate 2.4 --lat 37.7 --lon -122.4\\n";
            return 0;
        }
    }

    if (source_arg.empty()) {
        std::cerr << "Error: --source argument is required\\n";
        std::cerr << "Run with --help for usage information\\n";
        return 1;
    }

    if (sample_rate_mhz < 0.0f) {
        std::cerr << "Error: --rate argument is required (2 or 2.4)\\n";
        std::cerr << "Run with --help for usage information\\n";
        return 1;
    }

    if (sample_rate_mhz != 2.0f && sample_rate_mhz != 2.4f) {
        std::cerr << "Error: --rate must be either 2 or 2.4\\n";
        return 1;
    }

    std::cout << "=== ADSB Receiver ===" << std::endl;
    std::cout << "Map center: " << std::fabs(initial_lat)
              << "°" << (initial_lat >= 0.0f ? 'N' : 'S') << ", "
              << std::fabs(initial_lon)
              << "°" << (initial_lon >= 0.0f ? 'E' : 'W') << std::endl;
    std::cout << std::endl;

    // ADS-B frequency and settings
    constexpr uint64_t ADSB_FREQ_HZ = 1'090'000'000;  // 1090 MHz
    uint32_t SAMPLE_RATE_HZ = static_cast<uint32_t>(sample_rate_mhz * 1'000'000);
    constexpr double GAIN_DB = 30.0;

    bool use_soapy = (source_arg == "soapy");

    if (use_soapy) {
        std::cout << "Source: SoapySDR (auto-detected)" << std::endl;
        std::cout << "  Frequency: " << ADSB_FREQ_HZ / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample Rate: " << sample_rate_mhz << " MHz" << std::endl;
        std::cout << "  Gain: " << GAIN_DB << " dB" << std::endl;
    } else {
        std::cout << "Source: File playback" << std::endl;
        std::cout << "  File: " << source_arg << std::endl;
        std::cout << "  Sample Rate: " << sample_rate_mhz << " MHz" << std::endl;
    }
    std::cout << std::endl;

    cler::GuiManager gui(1400, 800, "ADSB Aircraft Tracker");

    std::string device_args_or_filename = "";
    if (!use_soapy) {
        device_args_or_filename = source_arg;
    }

    // SoapySDR reports device failures via exceptions by design
    std::optional<SelectableSourceBlock> source;
    try {
        source.emplace(
            "Source",
            use_soapy,
            use_soapy ? "" : source_arg,  // Empty string for auto-detect, or filename
            ADSB_FREQ_HZ,
            SAMPLE_RATE_HZ,
            GAIN_DB
        );
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (use_soapy) {
            std::cerr << "Make sure:" << std::endl;
            std::cerr << "  1. SoapySDR device is connected" << std::endl;
            std::cerr << "  2. SoapySDR drivers are installed for your device" << std::endl;
            std::cerr << "  3. You have permissions to access USB devices" << std::endl;
        }
        return 1;
    }

    IQToMagnitudeBlock iq2mag("IQ to Magnitude", SAMPLE_RATE_HZ);

    ADSBDecoderBlock::SampleRateMode decoder_mode = (sample_rate_mhz == 2.4f)
        ? ADSBDecoderBlock::SampleRateMode::RATE_2_4MHZ
        : ADSBDecoderBlock::SampleRateMode::RATE_2MHZ;
    ADSBDecoderBlock decoder("ADSB Decoder", decoder_mode);

    SinkNullBlock<uint16_t> null_sink("Null Sink");

    ADSBAggregateBlock aggregator(
        "ADSB Map",
        initial_lat, initial_lon,
        on_aircraft_update,
        nullptr
        // Coastline path defaults to "adsb_coastlines/ne_110m_coastline.shp"
    );

    aggregator.set_initial_window(0.0f, 0.0f, 1400.0f, 800.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&*source, &iq2mag.in),
        cler::BlockRunner(&iq2mag, &decoder.in),
        cler::BlockRunner(&decoder, &aggregator.in),
        cler::BlockRunner(&aggregator)
    );

    std::cout << "Starting receiver..." << std::endl;
    flowgraph.run();

    std::cout << "Tracking aircraft. Close window to exit." << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Mouse wheel: zoom in/out" << std::endl;
    std::cout << "  - Right-click drag: pan map" << std::endl;
    std::cout << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    std::cout << "Shutting down..." << std::endl;
    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);

    std::cout << "Total aircraft tracked: " << aggregator.aircraft_count() << std::endl;
    std::cerr << "[DONE] Receiver completed successfully" << std::endl;

    return 0;
}
`,me=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/math/add.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

int main() {
    cler::GuiManager gui(800, 400, "Hello World Plot Example");

    const size_t SPS = 1000;
    SourceCWBlock<float> source1("CWSource", 1.0f, 1.0f, SPS); //amplitude, frequency
    SourceCWBlock<float> source2("CWSource2", 1.0f, 20.0f, SPS);
    ThrottleBlock<float> throttle("Throttle", SPS);
    AddBlock<float, 2> adder("Adder");

    PlotTimeSeriesBlock plot(
        "Hello World Plot",
        {"Added Sources"},
        SPS,
        3.0f // duration in seconds
    );
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source1, &adder.in[0]),
        cler::BlockRunner(&source2, &adder.in[1]),
        cler::BlockRunner(&adder, &throttle.in),
        cler::BlockRunner(&throttle, &plot.in[0]),
        cler::BlockRunner(&plot)
    );

    flowgraph.run();
    while (!gui.should_close()) {
        gui.render(flowgraph);
    }
    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    return 0;
}`,ge=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <algorithm>
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include <atomic>
#include <cmath>
#include <complex>
#include <utility>

constexpr size_t SPS = 100;
constexpr float DT = 1.0f / static_cast<float>(SPS);
constexpr float wn = 1.0f;
constexpr float zeta = 0.5f;
constexpr float M = 1.0f;
constexpr float K = wn * wn * M;
constexpr float C = 2.0f * zeta * wn * M;
constexpr float DERIVATIVE_TAU = 0.05f;

struct PlantBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<float> force_in;
    PlantBlock(const char* name)  
        : BlockBase(name), force_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {
            force_in.push(0.0f);
        }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* measured_position_out) {
        auto [read_ptr, read_size] = force_in.read_dbf();
        auto [write_ptr, write_size] = measured_position_out->write_dbf();

        size_t to_process = std::min(read_size, write_size);
        if (to_process == 0 || !read_ptr || !write_ptr) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        float x = _x.load(std::memory_order_relaxed);
        for (size_t i = 0; i < to_process; ++i) {
            float force = read_ptr[i];

            float acceleration = (force - K * x - C * _v) / M;
            _v += acceleration * DT;
            x += _v * DT;
            write_ptr[i] = x;
        }
        _x.store(x, std::memory_order_relaxed);
        force_in.commit_read(to_process);
        measured_position_out->commit_write(to_process);
        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Plant");

        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
        if (canvas_sz.x < 200.0f) canvas_sz.x = 200.0f;
        if (canvas_sz.y < 100.0f) canvas_sz.y = 100.0f;
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(40, 40, 40, 255));
        draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255));

        float spring_start_x = canvas_p0.x + 50.0f;
        float center_y = (canvas_p0.y + canvas_p1.y) * 0.5f;

        float spring_end_x = spring_start_x + 200.0f + _x.load(std::memory_order_relaxed) * 20.0f;

        draw_list->AddLine(
            ImVec2(canvas_p0.x + 20.0f, center_y + 50.0f),
            ImVec2(canvas_p1.x - 20.0f, center_y + 50.0f),
            IM_COL32(200, 200, 200, 60), 1.0f
        );

        for (float y = center_y - 40.0f; y <= center_y + 40.0f; y += 8.0f) {
            draw_list->AddLine(
                ImVec2(spring_start_x - 20.0f, y),
                ImVec2(spring_start_x - 35.0f, y + 5.0f),
                IM_COL32(255, 255, 255, 150), 2.0f
            );
        }

        ImVec2 mass_size {40.0f, 40.0f};
        ImVec2 mass_p0 {spring_end_x, center_y - mass_size.y * 0.5f};
        ImVec2 mass_p1 {spring_end_x + mass_size.x, center_y + mass_size.y * 0.5f};
        ImVec2 mass_center {(mass_p0.x + mass_p1.x) * 0.5f, (mass_p0.y + mass_p1.y) * 0.5f};

        // Spring coil geometry is generated so it always ends at mass_center
        int num_points = 100;
        float coil_length = mass_center.x - 0.5f*(mass_size.x) - spring_start_x;
        float amplitude = 12.0f;
        float cycles = 5.0f; // number of full waves between start and end

        ImVec2 prev {spring_start_x, center_y};
        for (int i = 1; i <= num_points; ++i) {
            float t = (float)i / (float)num_points;
            ImVec2 point {spring_start_x + t * coil_length,
                          center_y + sinf(t * cycles * 2.0f * cler::PI) * amplitude};
            draw_list->AddLine(prev, point, IM_COL32(255, 215, 0, 255), 3.0f);
            prev = point;
        }

        ImVec2 shadow_p0 {mass_p0.x + 4.0f, mass_p0.y + 4.0f};
        ImVec2 shadow_p1 {mass_p1.x + 4.0f, mass_p1.y + 4.0f};
        draw_list->AddRectFilled(shadow_p0, shadow_p1, IM_COL32(0, 0, 0, 100), 6.0f);

        draw_list->AddRectFilled(mass_p0, mass_p1, IM_COL32(200, 50, 50, 255), 6.0f);
        draw_list->AddRect(mass_p0, mass_p1, IM_COL32(255, 255, 255, 180), 2.0f);

        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

    private:
        std::atomic<float> _x {0.0f};
        float _v = 0.0f;
        
        ImVec2 _initial_window_position {0.0f, 0.0f};
        ImVec2 _initial_window_size {600.0f, 300.0f};
};

struct ControllerBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<float> measured_position_in;
    float* _buffer;
    float* _error_buffer;
    size_t _buffer_size;

    ControllerBlock(const char* name)
        : BlockBase(name), measured_position_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {
        _buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float);
        _buffer = new float[_buffer_size];
        _error_buffer = new float[_buffer_size];
    }

    ~ControllerBlock() {
        delete[] _buffer;
        delete[] _error_buffer;
    }

    float kp() const { return _kp.load(std::memory_order_relaxed); }
    float ki() const { return _ki.load(std::memory_order_relaxed); }
    float kd() const { return _kd.load(std::memory_order_relaxed); }
    void set_gains(float kp, float ki, float kd) {
        _kp.store(kp, std::memory_order_relaxed);
        _ki.store(ki, std::memory_order_relaxed);
        _kd.store(kd, std::memory_order_relaxed);
    }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* force_out, cler::ChannelBase<float>* error_out) {
        size_t transferable = std::min(
            {measured_position_in.size(), force_out->space(), error_out->space(), _buffer_size});
        if (transferable == 0) return cler::Error::NotEnoughSpaceOrSamples;

        measured_position_in.readN(_buffer, transferable);

        float target  = _target.load(std::memory_order_relaxed);
        float kp      = _kp.load(std::memory_order_relaxed);
        float ki      = _ki.load(std::memory_order_relaxed);
        float kd      = _kd.load(std::memory_order_relaxed);
        bool feed_forward_enabled = _feed_forward.load(std::memory_order_relaxed);

        constexpr float ALPHA = DERIVATIVE_TAU / (DERIVATIVE_TAU + DT);
        constexpr float INTEGRAL_LIMIT = 20.0f;

        for (size_t i = 0; i < transferable; ++i) {
            float measured_position = _buffer[i];
            float ek = target - measured_position;

            // Derivative of the measurement, not the error: a target step
            // must not kick the D term.
            float derivative = -(measured_position - _xkm1) / DT;
            float dk = ALPHA * _dkm1 + (1.0f - ALPHA) * derivative;
            _int_state += ek * DT;
            _int_state = std::clamp(_int_state, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);

            float feed_forward = 0.0f;
            if (feed_forward_enabled) {
                feed_forward = K * target;
            }

            float force = kp * ek + ki * _int_state + kd * dk + feed_forward;

            _xkm1 = measured_position;
            _dkm1 = dk;

            _buffer[i] = force;
            _error_buffer[i] = ek;
        }

        force_out->writeN(_buffer, transferable);
        error_out->writeN(_error_buffer, transferable);

        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Controller");
        ImGui::Text("PID Controller");
        float tmp_target = _target.load();
        if (ImGui::SliderFloat("Target", &tmp_target, -10.0f, 10.0f)) {
            _target.store(tmp_target);
        }

        const std::pair<const char*, std::atomic<float>*> gains[] = {
            {"Kp", &_kp}, {"Ki", &_ki}, {"Kd", &_kd}};
        for (auto [label, gain] : gains) {
            float value = gain->load();
            if (ImGui::InputFloat(label, &value, 0.1f, 1.0f)) {
                gain->store(value);
            }
        }

        bool feed_forward = _feed_forward.load();
        ImGui::Checkbox("Feed Forward", &feed_forward);
        _feed_forward.store(feed_forward);

        if (ImGui::Button("Auto Tune")) {
            constexpr float WN_CL = 10.0f;
            set_gains(M * WN_CL * WN_CL - K, WN_CL / 2.0f, 2.0f * WN_CL * M - C);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pole placement on the known plant: closed loop 10 rad/s, critically damped");
        }

        ImGui::End();
    }


    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    float _xkm1 = 0.0f;
    float _dkm1 = 0.0f;
    float _int_state = 0.0f;

    std::atomic<float> _target {10.0f};
    std::atomic<float> _kp {99.0f};
    std::atomic<float> _ki {5.0f};
    std::atomic<float> _kd {19.0f};
    std::atomic<bool>  _feed_forward {true};

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};

struct RootLocusBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    RootLocusBlock(const char* name, ControllerBlock* controller)
        : BlockBase(name), _controller(controller) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }

    void render() {
        float kp = _controller->kp();
        float ki = _controller->ki();
        float kd = _controller->kd();
        if (kp != _kp || ki != _ki || kd != _kd) {
            _kp = kp;
            _ki = ki;
            _kd = kd;
            sweep();
        }

        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Root Locus");
        ImGui::Text("Branches from open-loop poles (x, gain 0) to zeros (o, gain inf)");
        ImGui::Text("Drag a square along its branch: kp, ki, kd scale together (kp %.1f  ki %.2f  kd %.2f)",
                    _kp, _ki, _kd);
        if (ImPlot::BeginPlot("##locus", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Re [rad/s]", "Im [rad/s]");
            ImPlot::SetupAxisLimits(ImAxis_X1, -25.0, 3.0, ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -22.0, 22.0, ImPlotCond_Once);

            draw_grid();

            float zero = 0.0f;
            ImPlot::PlotInfLines("##stability", &zero, 1,
                                 {ImPlotProp_LineColor, ImVec4(0.75f, 0.25f, 0.25f, 0.9f),
                                  ImPlotProp_LineWeight, 1.5f});

            for (int b = 0; b < ORDER; ++b) {
                char label[16];
                snprintf(label, sizeof(label), b == 0 ? "branches" : "##branch%d", b);
                ImPlot::PlotLine(label, _branch_re[b], _branch_im[b], STEPS,
                                 {ImPlotProp_LineColor, ImVec4(0.35f, 0.65f, 0.95f, 0.9f),
                                  ImPlotProp_LineWeight, 2.0f});
            }

            ImPlot::PlotScatter("open-loop poles", _ol_pole_re, _ol_pole_im, ORDER,
                                {ImPlotProp_Marker, ImPlotMarker_Cross,
                                 ImPlotProp_MarkerSize, 7.0f,
                                 ImPlotProp_LineColor, ImVec4(0.95f, 0.85f, 0.3f, 1.0f)});
            ImPlot::PlotScatter("open-loop zeros", _ol_zero_re, _ol_zero_im, 2,
                                {ImPlotProp_Marker, ImPlotMarker_Circle,
                                 ImPlotProp_MarkerSize, 6.0f,
                                 ImPlotProp_FillAlpha, 0.0f,
                                 ImPlotProp_LineColor, ImVec4(0.95f, 0.85f, 0.3f, 1.0f)});
            ImPlot::PlotScatter("current gains (drag along the locus)", _nom_re, _nom_im, ORDER,
                                {ImPlotProp_Marker, ImPlotMarker_Square,
                                 ImPlotProp_MarkerSize, 6.0f,
                                 ImPlotProp_LineColor, ImVec4(0.95f, 0.4f, 0.4f, 1.0f)});
            for (int r = 0; r < ORDER; ++r) {
                double px = _nom_re[r];
                double py = _nom_im[r];
                if (ImPlot::DragPoint(r, &px, &py, ImVec4(0.95f, 0.4f, 0.4f, 0.9f), 8.0f)) {
                    int at = nearest_vertex(static_cast<float>(px), static_cast<float>(py));
                    float g = std::clamp(_g[at % STEPS], 0.1f, 10.0f);
                    g = std::min({g, MAX_KP / _kp, MAX_KI / _ki, MAX_KD / _kd});
                    float re = _branch_re[at / STEPS][at % STEPS];
                    float im = _branch_im[at / STEPS][at % STEPS];
                    float mag = std::hypot(re, im);
                    ImGui::BeginTooltip();
                    ImGui::Text("loop gain %.3gx", g);
                    ImGui::Text("wn %.2f rad/s   zeta %.2f", mag, mag > 0.0f ? -re / mag : 0.0f);
                    ImGui::EndTooltip();
                    _controller->set_gains(_kp * g, _ki * g, _kd * g);
                }
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    static constexpr int STEPS = 240;
    static constexpr int ORDER = 4;
    static constexpr double G_MIN = 1e-3;
    static constexpr double G_MAX = 1e3;
    static constexpr float MAX_KP = 2000.0f;
    static constexpr float MAX_KI = 200.0f;
    static constexpr float MAX_KD = 400.0f;

    // Characteristic polynomial of the loop with the filtered PID:
    //   s(tau s+1)(M s^2 + C s + K) + g [ (kp tau + kd) s^2 + (kp + ki tau) s + ki ] = 0
    void quartic_at(double g, std::complex<double>* roots) const {
        constexpr double tau = DERIVATIVE_TAU;
        double a[ORDER + 1] = {
            g * _ki,
            K + g * (_kp + _ki * tau),
            tau * K + C + g * (_kp * tau + _kd),
            tau * C + M,
            tau * M,
        };
        solve_quartic(a, roots);
    }

    void sweep() {
        constexpr double tau = DERIVATIVE_TAU;

        std::complex<double> disc = std::sqrt(std::complex<double>(C * C - 4.0 * M * K, 0.0));
        std::complex<double> ol_poles[ORDER] = {
            {0.0, 0.0},
            {-1.0 / tau, 0.0},
            (disc - static_cast<double>(C)) / (2.0 * static_cast<double>(M)),
            (-disc - static_cast<double>(C)) / (2.0 * static_cast<double>(M)),
        };
        for (int i = 0; i < ORDER; ++i) {
            _ol_pole_re[i] = static_cast<float>(ol_poles[i].real());
            _ol_pole_im[i] = static_cast<float>(ol_poles[i].imag());
        }

        double zb = _kp + _ki * tau;
        double za = _kp * tau + _kd;
        std::complex<double> zdisc = std::sqrt(std::complex<double>(zb * zb - 4.0 * za * _ki, 0.0));
        std::complex<double> ol_zeros[2] = {
            (-zb + zdisc) / (2.0 * za),
            (-zb - zdisc) / (2.0 * za),
        };
        for (int i = 0; i < 2; ++i) {
            _ol_zero_re[i] = static_cast<float>(ol_zeros[i].real());
            _ol_zero_im[i] = static_cast<float>(ol_zeros[i].imag());
        }

        std::complex<double> prev[ORDER];
        for (int i = 0; i < ORDER; ++i) prev[i] = ol_poles[i];

        for (int step = 0; step < STEPS; ++step) {
            double g = G_MIN * std::pow(G_MAX / G_MIN, step / double(STEPS - 1));
            _g[step] = static_cast<float>(g);
            std::complex<double> roots[ORDER];
            quartic_at(g, roots);
            match_to(prev, roots);
            for (int b = 0; b < ORDER; ++b) {
                _branch_re[b][step] = static_cast<float>(roots[b].real());
                _branch_im[b][step] = static_cast<float>(roots[b].imag());
                prev[b] = roots[b];
            }
        }

        std::complex<double> nominal[ORDER];
        quartic_at(1.0, nominal);
        for (int r = 0; r < ORDER; ++r) {
            _nom_re[r] = static_cast<float>(nominal[r].real());
            _nom_im[r] = static_cast<float>(nominal[r].imag());
        }
    }

    int nearest_vertex(float x, float y) const {
        int best = 0;
        float best_d = 0.0f;
        for (int b = 0; b < ORDER; ++b) {
            for (int i = 0; i < STEPS; ++i) {
                float dx = _branch_re[b][i] - x;
                float dy = _branch_im[b][i] - y;
                float d = dx * dx + dy * dy;
                if ((b == 0 && i == 0) || d < best_d) {
                    best_d = d;
                    best = b * STEPS + i;
                }
            }
        }
        return best;
    }

    static void match_to(const std::complex<double>* prev, std::complex<double>* roots) {
        bool taken[ORDER] = {};
        std::complex<double> ordered[ORDER];
        for (int b = 0; b < ORDER; ++b) {
            int best = -1;
            double best_d = 0.0;
            for (int r = 0; r < ORDER; ++r) {
                if (taken[r]) continue;
                double d = std::abs(roots[r] - prev[b]);
                if (best < 0 || d < best_d) { best = r; best_d = d; }
            }
            taken[best] = true;
            ordered[b] = roots[best];
        }
        for (int b = 0; b < ORDER; ++b) roots[b] = ordered[b];
    }

    static void solve_quartic(const double a[ORDER + 1], std::complex<double>* roots) {
        std::complex<double> monic[ORDER];
        for (int i = 0; i < ORDER; ++i) monic[i] = a[i] / a[ORDER];
        std::complex<double> seed(0.4, 0.9);
        for (int i = 0; i < ORDER; ++i) roots[i] = std::pow(seed, i + 1);
        for (int pass = 0; pass < 80; ++pass) {
            for (int i = 0; i < ORDER; ++i) {
                std::complex<double> value = 1.0;
                for (int p = ORDER - 1; p >= 0; --p) value = value * roots[i] + monic[p];
                std::complex<double> denom = 1.0;
                for (int j = 0; j < ORDER; ++j) {
                    if (j != i) denom *= roots[i] - roots[j];
                }
                roots[i] -= value / denom;
            }
        }
    }

    static void draw_grid() {
        const ImVec4 faint(0.5f, 0.5f, 0.5f, 0.35f);
        const float R = 40.0f;
        const float zetas[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
        for (float z : zetas) {
            float xs[2] = {0.0f, -R * z};
            float ys_up[2] = {0.0f, R * std::sin(std::acos(z))};
            float ys_dn[2] = {0.0f, -ys_up[1]};
            char label[24];
            snprintf(label, sizeof(label), "##zeta%.1f", z);
            ImPlot::PlotLine(label, xs, ys_up, 2, {ImPlotProp_LineColor, faint});
            snprintf(label, sizeof(label), "##zetan%.1f", z);
            ImPlot::PlotLine(label, xs, ys_dn, 2, {ImPlotProp_LineColor, faint});
            snprintf(label, sizeof(label), "z=%.1f", z);
            ImPlot::PlotText(label, xs[1], ys_up[1]);
        }
        const float radii[] = {5.0f, 10.0f, 15.0f, 20.0f};
        for (float r : radii) {
            float cx[41];
            float cy[41];
            for (int i = 0; i <= 40; ++i) {
                float theta = cler::PI / 2.0f + (cler::PI / 40.0f) * i;
                cx[i] = r * std::cos(theta);
                cy[i] = r * std::sin(theta);
            }
            char label[24];
            snprintf(label, sizeof(label), "##wn%.0f", r);
            ImPlot::PlotLine(label, cx, cy, 41, {ImPlotProp_LineColor, faint});
            snprintf(label, sizeof(label), "%.0f", r);
            ImPlot::PlotText(label, -r, 0.8f);
        }
    }

    ControllerBlock* _controller;
    float _kp = -1.0f;
    float _ki = -1.0f;
    float _kd = -1.0f;
    float _g[STEPS] = {};
    float _branch_re[ORDER][STEPS] = {};
    float _branch_im[ORDER][STEPS] = {};
    float _ol_pole_re[ORDER] = {};
    float _ol_pole_im[ORDER] = {};
    float _ol_zero_re[2] = {};
    float _ol_zero_im[2] = {};
    float _nom_re[ORDER] = {};
    float _nom_im[ORDER] = {};

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 400.0f};
};

int main() {
    const float GW = 1400.0f;
    const float GH = 800.0f;
    cler::GuiManager gui (static_cast<size_t>(GW), static_cast<size_t>(GH), "Mass-Spring-Damper Simulation");
    ControllerBlock controller("Controller");
    ThrottleBlock<float> throttle("Throttle", SPS);
    PlantBlock plant("Plant");
    RootLocusBlock root_locus("RootLocus", &controller);

    FanoutBlock<float> fanout("Fanout", 2);

    PlotTimeSeriesBlock plot(
        "Sensor Plot",
        {"Measured Position"},
        SPS,
        100.0f
    );

    PlotTimeSeriesBlock error_plot(
        "Error Plot",
        {"target - x"},
        SPS,
        100.0f
    );

    plant.set_initial_window(0.0f, 0.0f, GW, 220.0f);
    controller.set_initial_window(0.0f, 220.0f, 260.0f, GH - 220.0f);
    plot.set_initial_window(260.0f, 220.0f, 570.0f, (GH - 220.0f) / 2.0f);
    error_plot.set_initial_window(260.0f, 220.0f + (GH - 220.0f) / 2.0f, 570.0f, (GH - 220.0f) / 2.0f);
    root_locus.set_initial_window(830.0f, 220.0f, GW - 830.0f, GH - 220.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&controller, &throttle.in, &error_plot.in[0]),
    cler::BlockRunner(&throttle, &plant.force_in),
    cler::BlockRunner(&plant, &fanout.in),
    cler::BlockRunner(&fanout, &plot.in[0], &controller.measured_position_in),
    cler::BlockRunner(&plot),
    cler::BlockRunner(&error_plot),
    cler::BlockRunner(&root_locus)
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }
    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    return 0;
}
`,xe=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/math/complex_demux.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"

int main() {
    size_t SPS = 2000;

    const size_t GW = 1500;
    const size_t GH = 800;
    cler::GuiManager gui(GW, GH , "Plots Example");
    
    SourceCWBlock<std::complex<float>> cw_source("CWSource", 1.0f, 2.0f, SPS);
    ThrottleBlock<std::complex<float>> cw_throttle("CWThrottle", SPS);
    FanoutBlock<std::complex<float>> cw_fanout("CWFanout", 3);
    ComplexToMagPhaseBlock cw_complex2realimag("CWComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    PlotTimeSeriesBlock cw_timeseries_plot(
        "CW-TimeSeriesPlot",
        {"Real", "Imaginary"},
        SPS,
        10.0f //duration in seconds
    );

    SourceChirpBlock<std::complex<float>> chirp_source("ChirpSource", 1.0f, 200.0f, 800.0f, SPS, 4.0f);
    ThrottleBlock<std::complex<float>> chirp_throttle("ChirpThrottle", SPS);
    FanoutBlock<std::complex<float>> chirp_fanout("ChirpFanout", 3);
    ComplexToMagPhaseBlock chirp_c2realimag("ChirpComplex2RealImag", ComplexToMagPhaseBlock::Mode::RealImag);
    PlotTimeSeriesBlock chirp_timeseries_plot(
        "Chirp-TimeSeriesPlot",
        {"Real", "Imaginary"},
        SPS,
        10.0f //duration in seconds
    );
    
    PlotCSpectrumBlock cspectrum_plot(
        "Chirp-CSpectrumPlot",
        {"CW", "Chirp"},
        SPS,
        1024 // buffer size for FFT
    );

    PlotCSpectrogramBlock cspectrogram_plot(
        "CW-SpectrogramPlot",
        {"CW", "Chirp"},
        SPS,
        256, // buffer size for FFT
        100 // tall
    );

    cw_timeseries_plot.set_initial_window(0.0f, 0.0f, GW / 2.0f, GH / 2.0f);
    chirp_timeseries_plot.set_initial_window(GW /2.0, 0.0f, GW / 2.0f, GH / 2.0f);
    cspectrum_plot.set_initial_window(0.0, GH/2.0, GW / 2.0f, GH / 2.0f);
    cspectrogram_plot.set_initial_window(GW/2.0, GH/2.0, GW / 2.0f, GH / 2.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw_source, &cw_throttle.in),
        cler::BlockRunner(&cw_throttle, &cw_fanout.in),
        cler::BlockRunner(&cw_fanout, &cw_complex2realimag.in, &cspectrum_plot.in[0], &cspectrogram_plot.in[0]),
        cler::BlockRunner(&cw_complex2realimag, &cw_timeseries_plot.in[0], &cw_timeseries_plot.in[1]),
        cler::BlockRunner(&cw_timeseries_plot), 

        cler::BlockRunner(&chirp_source, &chirp_throttle.in),
        cler::BlockRunner(&chirp_throttle, &chirp_fanout.in),
        cler::BlockRunner(&chirp_fanout, &chirp_c2realimag.in, &cspectrum_plot.in[1], &cspectrogram_plot.in[1]),
        cler::BlockRunner(&chirp_c2realimag, &chirp_timeseries_plot.in[0], &chirp_timeseries_plot.in[1]),
        cler::BlockRunner(&chirp_timeseries_plot),

        cler::BlockRunner(&cspectrum_plot),
        cler::BlockRunner(&cspectrogram_plot)
    );

    flowgraph.run();
    while (!gui.should_close()) {
        gui.render(flowgraph);
    }
    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
}
`,ke=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/channelizers/polyphase_channelizer.hpp"
#include "desktop_blocks/math/add.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/noise/awgn.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/utils/throughput.hpp"

float channel_freq(float channel_bw, uint8_t index, uint8_t num_channels) {
    float offset = static_cast<float>(index) - static_cast<float>(num_channels) / 2.0 + 0.5f;
    return offset * channel_bw;
}

struct CustomSourceBlock : public cler::BlockBase {
    CustomSourceBlock(const char* name,
                const float amplitude,
                const float noise_stddev,
                const float frequency_hz,
                const size_t sps)
        : BlockBase(name),
        cw_source_block(cler::EmbeddableString<64>(this->name()) + "_CWSource",
                        amplitude, frequency_hz, sps),
        noise_block(cler::EmbeddableString<64>(this->name()) + "_AWGN",
                    noise_stddev),
        fanout_block(cler::EmbeddableString<64>(this->name()) + "_Fanout", 2)
    { }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out1, 
                                                      cler::ChannelBase<std::complex<float>>* out2) {
    
        size_t transferable = std::min({
            out1->space(),
            out2->space(),
            noise_block.in.space(),
            fanout_block.in.space()
        });
        if (transferable == 0) {
            return cler::Error::NotEnoughSpace;
        }
    
        auto result = cw_source_block.procedure(&noise_block.in);
        if (result.is_err()) {
            return result.unwrap_err();
        }

        result = noise_block.procedure(&fanout_block.in);
        if (result.is_err()) {
            return result.unwrap_err();
        }

        return fanout_block.procedure(out1, out2);
    }    

    private:
        SourceCWBlock<std::complex<float>> cw_source_block;
        NoiseAWGNBlock<std::complex<float>> noise_block;
        FanoutBlock<std::complex<float>> fanout_block;

};

int main() {
    static constexpr size_t NUM_CHANNELS = 5;
    static constexpr size_t SPS = 2'000'000;
    static constexpr float channel_BW = static_cast<float>(SPS) / static_cast<float>(NUM_CHANNELS);

    float ch0_freq = channel_freq(channel_BW, 0, NUM_CHANNELS);
    float ch1_freq = channel_freq(channel_BW, 1, NUM_CHANNELS);
    float ch2_freq = channel_freq(channel_BW, 2, NUM_CHANNELS);
    float ch3_freq = channel_freq(channel_BW, 3, NUM_CHANNELS);
    float ch4_freq = channel_freq(channel_BW, 4, NUM_CHANNELS);

    CustomSourceBlock cw_source0("CW Source 0", 1.0f, 0.01f, ch0_freq, SPS);
    CustomSourceBlock cw_source1("CW Source 1", 3.0f, 0.01f, ch1_freq, SPS);
    CustomSourceBlock cw_source2("CW Source 2", 10.0f, 0.01f, ch2_freq, SPS);
    CustomSourceBlock cw_source3("CW Source 3", 30.0f, 0.01f, ch3_freq, SPS);
    CustomSourceBlock cw_source4("CW Source 4", 100.0f, 0.01f, ch4_freq, SPS);


    AddBlock<std::complex<float>, NUM_CHANNELS> adder("Adder");

    ThroughputBlock<std::complex<float>> throughput("Throughput");

    PolyphaseChannelizerBlock<NUM_CHANNELS, 3> channelizer(
        "Polyphase Channelizer",
        80.0f // kaiser attenuation
    );

    PlotCSpectrumBlock plot_polyphase_cspectrum(
        "Plot Channelizer Spectrum",
        {"pfch 0", "pfch 1", "pfch 2", "pfch 3", "pfch 4"},
        static_cast<size_t>(channel_BW),
        1024,
        SpectralWindow::BlackmanHarris
    );

    PlotCSpectrumBlock plot_input_cspectrum(
        "Plot Input Spectrum",
        {"source 0", "source 1", "source 2", "source 3", "source 4"},
        SPS,
        1024,
        SpectralWindow::BlackmanHarris
    );
 
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw_source0, &adder.in[0], &plot_input_cspectrum.in[0]),
        cler::BlockRunner(&cw_source1, &adder.in[1], &plot_input_cspectrum.in[1]),
        cler::BlockRunner(&cw_source2, &adder.in[2], &plot_input_cspectrum.in[2]),
        cler::BlockRunner(&cw_source3, &adder.in[3], &plot_input_cspectrum.in[3]),
        cler::BlockRunner(&cw_source4, &adder.in[4], &plot_input_cspectrum.in[4]),
        cler::BlockRunner(&adder, &throughput.in),
        cler::BlockRunner(&throughput, &channelizer.in),
        cler::BlockRunner(&channelizer,
            &plot_polyphase_cspectrum.in[0],
            &plot_polyphase_cspectrum.in[1],
            &plot_polyphase_cspectrum.in[2],
            &plot_polyphase_cspectrum.in[3],
            &plot_polyphase_cspectrum.in[4]
        ),
        cler::BlockRunner(&plot_polyphase_cspectrum),
        cler::BlockRunner(&plot_input_cspectrum)
    );

    cler::FlowGraphConfig config;
    config.scheduler = cler::SchedulerType::FixedThreadPool;
    config.collect_detailed_stats = true;
    flowgraph.run(config);
    
    const float GW = 1800.0f;
    const float GH = 1000.0f;
    cler::GuiManager gui_manager(GW, GH, "Polyphase Channelizer Example");
    plot_input_cspectrum.set_initial_window(0.0, 0.0, GW, GH/2.0f);
    plot_polyphase_cspectrum.set_initial_window(0.0, GH/2.0f, GW, GH/2.0f);
    while (!gui_manager.should_close()) {
        gui_manager.render(flowgraph);
    }

    flowgraph.stop();
    print_flowgraph_execution_report(flowgraph);
    throughput.report();

    return 0;
}`,he=`// Slim "Spike-like" spectrum analyzer GUI for USRP / HackRF / Pluto on CLER.
// All blocks always run; a staged rate change re-syncs every consumer once the
// ACTUAL hardware rate lands.

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"

#include "spike_source.hpp"
#include "spike_args.hpp"
#include "control_panel.hpp"
#include "snapshot.hpp"
#include "capture.hpp"

#include <filesystem>
#include <iostream>
#include <string>

using Trig = TriggerBlock<float>;
using PowerDet = PowerDetectorBlock<std::complex<float>>;

static const char* window_title_for(SourceKind kind) {
    switch (kind) {
        case SourceKind::HackRF: return "CLER Spike - HackRF";
        case SourceKind::Pluto:  return "CLER Spike - Pluto";
        case SourceKind::UHD:    break;
    }
    return "CLER Spike - USRP";
}

static int run_app(SpikeArgs& args) {
    cler::GuiManager gui(1500, 900, window_title_for(args.source));
    // Vsync already paces this loop; the tiny sleep just keeps CPU low if vsync is off/bypassed.
    gui.set_frame_sleep_ms(2);

    ImGuiIO& io = ImGui::GetIO();
    static std::string imgui_ini = config_path(".cler_spike_imgui.ini");
    io.IniFilename = imgui_ini.c_str();

    SpikeSourceBlock source("Source", args.source, args.freq, args.rate,
                            args.device_address, args.gain,
                            args.lna, args.vga, args.amp);
    args.rate = source.actual_sample_rate();
    ISource& src_if = source;

    FanoutBlock<std::complex<float>> fanout("Fanout", 4);
    PowerDetectorBlock<std::complex<float>> power("PowerDetector", -120.0f);

    Trig trigger("Trigger", static_cast<size_t>(args.rate),
                 /*threshold*/   -40.0f,
                 /*window_ms*/    20.0f,
                 /*pretrigger%*/  10.0f,
                 /*holdoff_ms*/   100.0f,
                 Trig::Edge::Rising,
                 Trig::Mode::Auto,
                 /*hysteresis*/   3.0f,
                 /*auto_ms*/      200.0f,
                 /*max_window_ms*/5000.0f);

    PlotCSpectrumBlock spectrum("Spectrum", {"I/Q"}, static_cast<size_t>(args.rate), args.fft);

    // Drains its whole input each call so it never stalls the shared fanout (min-space commit); paused via set_active() while hidden.
    //
    // The ring holds \`waterfall_rows\` rows; each row spans at minimum one FFT
    // frame = n_fft/rate seconds (frames/row = 1). So the SHORTEST visible
    // history is waterfall_rows * n_fft / rate. The default 2000 rows gives
    // ~4.1 s at 1 MS/s / 2048-pt; --history sizes the ring to reach a shorter
    // floor (rows = history * rate / n_fft), clamped to a usable range. The
    // in-window History slider still scales UP from this floor (frames/row).
    size_t waterfall_rows = 2000;
    if (args.history_s > 0.0) {
        double rows = std::round(args.history_s * args.rate
                                 / static_cast<double>(args.fft));
        rows = std::min(std::max(rows, 16.0), 20000.0);
        waterfall_rows = static_cast<size_t>(rows);
    }
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"I/Q"},
        static_cast<size_t>(args.rate), args.fft, waterfall_rows);

    ChannelizerPanelBlock channelizer("Channelizer");

    trigger.set_initial_window(380.0f, 10.0f, 1100.0f, 430.0f);
    spectrum.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    spectrogram.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);
    channelizer.set_initial_window(380.0f, 455.0f, 1100.0f, 430.0f);

    ControlPanel panel("ControlPanel", &src_if, &trigger, &spectrum, &spectrogram,
                       &power, &channelizer, waterfall_rows, args);
    const std::string settings_file = config_path(".cler_spike.conf");
    panel.load(settings_file);   // restore last session's settings if present

    // Capture mode overrides two loaded settings: the destination directory, and
    // the scope's visibility (a conf that hid it would yield useless snapshots).
    if (args.capture_mode()) {
        std::error_code ec;
        std::filesystem::create_directories(args.capture_dir, ec);
        if (!std::filesystem::is_directory(args.capture_dir)) {
            std::cerr << "Error: --capture dir " << args.capture_dir
                      << " is not usable: " << ec.message() << "\\n";
            return 1;
        }
        panel.set_snapshot_dir(args.capture_dir);
        panel.show_scope = true;
    }

    CaptureBlock capture("Capture", args, &panel, &trigger, &spectrum,
                         &spectrogram, &gui);

    // Trigger is a sink: renders its own capture (oscilloscope-style), no downstream channel.
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&panel),
        cler::BlockRunner(&source,  &fanout.in),
        cler::BlockRunner(&fanout,  &power.in, &spectrum.in[0], &spectrogram.in[0],
                                    &channelizer.in),
        cler::BlockRunner(&power,   &trigger.in),
        cler::BlockRunner(&trigger),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram),
        cler::BlockRunner(&channelizer),
        cler::BlockRunner(&capture)
    );

    flowgraph.run();
    panel.apply_all();   // sync radio + trigger to loaded/initial settings
    // Report what the panel actually applied; args.* may be stale by now.
    std::cout << "CLER Spike running at " << panel.freq_mhz() << " MHz, "
              << panel.rate_hz() / 1e6 << " MS/s. Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    // Capture runs must not persist settings: --capture overrides the snapshot
    // dir and scope visibility, and an unattended run would overwrite the
    // user's tuned conf with them.
    if (!args.capture_mode())
        panel.save(settings_file);   // remember settings for next session
    std::cout << "Overflows: " << src_if.get_overflow_count() << std::endl;
    return capture.timed_out() ? 2 : 0;
}

int main(int argc, char** argv) {
    SpikeArgs args = parse_args(argc, argv);
    return run_app(args);
}
`,ye=`#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/utils/throughput.hpp"

#include <complex>

struct RampSourceBlock : public cler::BlockBase {
    RampSourceBlock(const char* name) : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        out->push(_next);
        _next += 1.0f;
        return cler::Empty{};
    }

  private:
    float _next = 0.0f;
};

struct ComplexSinkBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    ComplexSinkBlock(const char* name) : cler::BlockBase(name), in(1024) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        std::complex<float> sample;
        while (in.pop(sample)) {
        }
        return cler::Empty{};
    }
};

int main() {
    RampSourceBlock ramp("Ramp Source");
    ThrottleBlock<float> throttle("Throttle", 1000);
    ThroughputBlock<std::complex<float>> throughput("Throughput");
    ComplexSinkBlock sink("Complex Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&ramp, &throttle.in),
        cler::BlockRunner(&throttle, &throughput.in),
        cler::BlockRunner(&throughput, &sink.in),
        cler::BlockRunner(&sink)
    );

    flowgraph.run();
    return 0;
}
`,be=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <cstdlib>
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sinks/sink_uhd.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/plots/plot_cspectrogram.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "power_detector.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <iostream>
#include <vector>
#include <map>

struct USRPArgs {
    std::string mode;
    double freq = 915e6;
    double rate = 2e6;
    double gain = 89.75;
    double cw_offset = 10e3; // for tx-cw
    double amp = 1;         // amplitude
    size_t fft = 1024;
    std::string device_address;
    double chirp_duration_s = 1; // for tx-chirp
};

void print_usage(const char* prog) {
    std::cout << "\\nUSRP Example - Unified demonstration of UHD block features\\n" << std::endl;
    std::cout << "Usage: " << prog << " [OPTIONS]" << std::endl;
    std::cout << "\\nAvailable modes:" << std::endl;
    std::cout << "  rx          - Simple RX with spectrum plot" << std::endl;
    std::cout << "  zero-span   - Zero span mode (power vs time)" << std::endl;
    std::cout << "  tx-chirp    - Transmit chirp signal with spectrum plot" << std::endl;
    std::cout << "  tx-cw       - Transmit continuous wave with spectrum plot" << std::endl;
    std::cout << "\\nOptions:" << std::endl;
    std::cout << "  -m, --mode MODE          Operating mode: rx, tx-chirp, or tx-cw (required)" << std::endl;
    std::cout << "  -f, --freq FREQ          Center frequency in Hz (default: 915e6)" << std::endl;
    std::cout << "  -r, --rate RATE          Sample rate in samples/sec (default: 2e6)" << std::endl;
    std::cout << "  -g, --gain GAIN          Gain in dB (default: depends on mode)" << std::endl;
    std::cout << "  -a, --amp AMP            Amplitude 0.0-1.0 (default: depends on mode)" << std::endl;
    std::cout << "  -o, --cw_offset OFFSET   CW tone offset from center in Hz (default: 0)" << std::endl;
    std::cout << "  -F, --fft SIZE           FFT size for spectrum analysis (default: 2048)" << std::endl;
    std::cout << "  -c, --chirp_duration DUR Chirp duration in seconds (default: 0.001)" << std::endl;
    std::cout << "  -d, --dev ADDRESS        USRP device address (default: auto)" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
    std::cout << "\\nExamples:" << std::endl;
    std::cout << "  " << prog << " -m rx -f 915e6 -r 2e6 -g 30" << std::endl;
    std::cout << "  " << prog << " --mode tx-chirp --freq 915e6 --rate 2e6 --gain 89 --amp 0.3" << std::endl;
    std::cout << "  " << prog << " -m tx-cw -f 915e6 -r 2e6 -g 89 -o 100e3 -a 0.5" << std::endl;
    std::cout << "  " << prog << " -m rx -d \\"addr=192.168.10.2\\" -f 2.4e9" << std::endl;
    std::cout << std::endl;
}

USRPArgs parse_args(int argc, char** argv) {
    USRPArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next_arg = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                exit(1);
            }
            return argv[++i];
        };

        auto num_arg = [&]() -> double {
            std::string v = next_arg();
            char* end = nullptr;
            double d = std::strtod(v.c_str(), &end);
            if (end == v.c_str() || *end != '\\0') {
                std::cerr << "Error: invalid numeric value for " << arg << ": " << v << std::endl;
                exit(1);
            }
            return d;
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        }
        else if (arg == "-m" || arg == "--mode") {
            args.mode = next_arg();
        }
        else if (arg == "-f" || arg == "--freq") {
            args.freq = num_arg();  // supports 918e6 or 918000000
        }
        else if (arg == "-r" || arg == "--rate") {
            args.rate = num_arg();  // supports 2e6 or 2000000
        }
        else if (arg == "-g" || arg == "--gain") {
            args.gain = num_arg();
        }
        else if (arg == "-a" || arg == "--amp") {
            args.amp = num_arg();
        }
        else if (arg == "-o" || arg == "--cw_offset") {
            args.cw_offset = num_arg();
        }
        else if (arg == "-F" || arg == "--fft") {
            args.fft = static_cast<size_t>(num_arg());
        }
        else if (arg == "-c" || arg == "--chirp_duration") {
            args.chirp_duration_s = num_arg();
        }
        else if (arg == "-d" || arg == "--dev" || arg == "--device") {
            args.device_address = next_arg();
        }
        else {
            std::cerr << "Error: Unknown option '" << arg << "'" << std::endl;
            std::cerr << "Use -h or --help for usage information" << std::endl;
            exit(1);
        }
    }
    
    return args;
}

void mode_rx(const USRPArgs& args) {
    SourceUHDBlock<std::complex<float>> usrp_source("USRP", args.freq,
        args.rate, args.device_address, args.gain, 1 /*num channels*/);

    cler::GuiManager gui(1600, 1600, "USRP Receiver Example");
    PlotCSpectrumBlock spectrum("USRP Spectrum", {"I/Q"}, args.rate, args.fft);
    PlotCSpectrogramBlock spectrogram("Spectrogram", {"usrp_signal"}, args.rate, args.fft, 4000);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    spectrogram.set_initial_window(1000.0f, 0.0f, 800.0f, 800.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp_source, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &spectrogram.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&spectrogram)
    );

    flowgraph.run();
    std::cout << "Flowgraph running... Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    std::cout << "Overflows: " << usrp_source.get_overflow_count() << std::endl;
}

void mode_tx_chirp(const USRPArgs& args) {
    UHDConfig config{args.freq, args.rate, args.gain};

    SinkUHDBlock<std::complex<float>> usrp_sink("USRP_TX",
        args.device_address, 1 /*num channels*/, 0 /*mboard*/, "sc16", &config);

    cler::GuiManager gui(1200, 600, "USRP TX - Chirp Signal");
    SourceChirpBlock<std::complex<float>> chirp("Chirp",
                                                 args.amp,
                                                 -500e3f,
                                                 500e3f,
                                                 args.rate,
                                                 args.chirp_duration_s);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    PlotCSpectrumBlock spectrum("TX Spectrum", {"Chirp"}, args.rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&chirp, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp_sink.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp_sink)
    );

    flowgraph.run();
    std::cout << "Transmitting chirp signal. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    std::cout << "Underflows: " << usrp_sink.get_underflow_count() << std::endl;
}

void mode_tx_cw(const USRPArgs& args) {
    UHDConfig config{args.freq, args.rate, args.gain};

    SinkUHDBlock<std::complex<float>> usrp_sink("USRP_TX",
        args.device_address, 1 /*num channels*/, 0 /*mboard*/, "sc16", &config);

    cler::GuiManager gui(1200, 600, "USRP TX - Continuous Wave");
    SourceCWBlock<std::complex<float>> cw("CW", args.amp, args.cw_offset, args.rate);
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);
    PlotCSpectrumBlock spectrum("TX Spectrum", {"CW Tone"}, args.rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp_sink.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp_sink)
    );

    flowgraph.run();
    std::cout << "Transmitting CW tone. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    std::cout << "Underflows: " << usrp_sink.get_underflow_count() << std::endl;
}

void mode_zero_span(const USRPArgs& args) {
    SourceUHDBlock<std::complex<float>> usrp_source("USRP", args.freq,
        args.rate, args.device_address, args.gain, 1);

    cler::GuiManager gui(1600, 1600, "USRP Zero Span - Power vs Time");

    PowerDetectorBlock<std::complex<float>> power_detector("PowerDetector", -80.0f);

    PlotTimeSeriesBlock power_plot("Power vs Time",
                                   {"Power (dB)"},
                                   args.rate,
                                   5.0f);        // show last 5 seconds

    power_plot.set_initial_window(0.0f, 0.0f, 1600.0f, 1600.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp_source, &power_detector.in),
        cler::BlockRunner(&power_detector, &power_plot.in[0]),
        cler::BlockRunner(&power_plot)
    );

    flowgraph.run();
    std::cout << "Zero Span mode at " << args.freq << " Hz" << std::endl;
    std::cout << "Showing instantaneous power vs time. Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }

    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    std::cout << "Overflows: " << usrp_source.get_overflow_count() << std::endl;
}

int main(int argc, char** argv) {
    USRPArgs args = parse_args(argc, argv);
    std::cout << "inside main" << std::endl;

    std::cout << "Mode: " << args.mode << "\\n"
              << "Freq: " << args.freq << " Hz\\n"
              << "Rate: " << args.rate << " S/s\\n"
              << "Gain: " << args.gain << " dB\\n"
              << "Amplitude: " << args.amp << "\\n"
              << "CW Offset: " << args.cw_offset << " Hz\\n"
              << "FFT: " << args.fft << "\\n"
              << "Device: " << (args.device_address.empty() ? "default" : args.device_address)
              << std::endl;

    if (args.mode == "rx") {
        mode_rx(args);
    } else if (args.mode == "tx-chirp") {
        mode_tx_chirp(args);
    } else if (args.mode == "tx-cw") {
        mode_tx_cw(args);
    } else if (args.mode == "zero-span") {
        mode_zero_span(args);
    } else {
        std::cerr << "Unknown mode: " << args.mode << "\\n";
        return 1;
    }

    return 0;
}
`,we={hello_world:v,plots:N,polyphase_channelizer:$,mass_spring_damper:E,uhd_device:ue,spike:J,adsb_receiver:k,type_conflict:oe},ve={hello_world:me,plots:xe,polyphase_channelizer:ke,mass_spring_damper:ge,uhd_device:be,spike:he,adsb_receiver:fe,type_conflict:ye},Pe=Object.keys(we),Se={toolchain:30,boot:6,stage:6,compile:30,link:24,optimize:6,store:1,launch:1},a=["toolchain","boot","stage","compile","link","optimize","store","launch"],Ce="cler.build.timings";function Re(e){switch(e.phase){case"toolchain":return{phrase:"Downloading the compiler…",target:e.total?`${d(e.bytes??0)} / ${d(e.total)} MB`:`${d(e.bytes??0)} MB`};case"boot":return{phrase:"Unpacking clang — warming the toolchain…",target:null};case"stage":return{phrase:e.detail?"Staging cler headers":"Staging cler headers…",target:e.detail??null};case"compile":return{phrase:"Compiling",target:e.detail??"the flowgraph"};case"link":return{phrase:"Persuading wasm-ld — linking against libcler_web…",target:null};case"optimize":return{phrase:"Optimizing wasm (Asyncify)…",target:null};case"store":return{phrase:"Storing the build in your browser…",target:null};case"launch":return{phrase:"Wiring runners — launching…",target:null}}}function d(e){return(e/1e6).toFixed(1)}function _(e){const r={...Se};for(const t of a){const n=e[t];n&&n>0&&(r[t]=n)}return r}function Ie(e,r,t){const n=_(t),i=a.reduce((l,p)=>l+n[p],0),c=a.slice(0,a.indexOf(e.phase)).reduce((l,p)=>l+n[p],0),o=Be(e,r,t);return o===null?null:Math.min(1,(c+o*n[e.phase])/i)}function Be(e,r,t){if(e.phase==="toolchain"&&e.total)return Math.min(1,(e.bytes??0)/e.total);const n=t[e.phase];return!n||n<=0?null:Math.min(.95,r/n)}function ze(e,r,t){if(Object.keys(t).length===0)return null;const n=_(t),i=a.slice(a.indexOf(e.phase)+1).reduce((o,l)=>o+(t[l]??0),0),c=t[e.phase]?Math.max(0,t[e.phase]-r):e.phase==="toolchain"&&e.total?(1-Math.min(1,(e.bytes??0)/e.total))*n.toolchain:0;return Math.round((i+c)/1e3)}let s=null;function Te(e){return s=e,()=>{s===e&&(s=null)}}function Ee(e){s?.(e)}export{Ce as T,we as a,Re as b,Pe as c,Ee as d,ve as f,Te as o,Ie as p,ze as r};
