import socket
import argparse
import sys
import struct

class ResponseElement:
	def __init__(self,name,byte_number,scale=1,element_type=int,signed=False):
		self.name = name
		self.byte_number = byte_number
		self.scale=scale
		self.element_type=element_type
		self.signed = signed
	def parse(self,raw_bytes,offset=0):
		slbytes = raw_bytes[offset:offset+self.byte_number]
		if self.element_type==int:
			ret = self.element_type.from_bytes(slbytes,'little',signed=self.signed)
		elif self.element_type==float:
			[ret] = struct.unpack('<f',slbytes)
		else:
			raise ValueError("ResponseElement: parse: unknown type {0}".format(self.element_type))
		fret = float(ret)/self.scale
		return(fret)
		

commands = {
  "PCOMMAND_RESERVED":{'val':0,'responses':
	[
	
	],
	'default_payload':bytearray()},
  "PCOMMAND_STATUS":{'val':1,'responses':
	[
		ResponseElement("uptime_s", 4),
		ResponseElement("motor_deg", 1),
		ResponseElement("board_T_C", 2, scale=10),
		ResponseElement("packet_count", 4),
	],
	'default_payload':bytearray()},
  "PCOMMAND_UPTIME":{'val':2,'responses':
	[
		ResponseElement("uptime_s", 4),
	],
	'default_payload':bytearray()},
  "PCOMMAND_READ_DIR_AD_RAW":{'val':3,'responses':
	[
		ResponseElement("dir_raw_10bit", 2),
	],
	'default_payload':bytearray()},
  "PCOMMAND_READ_SPEED_TIMER_RAW":{'val':4,'responses':
	[
		ResponseElement("rotor_raw_millis", 2),
	],
	'default_payload':bytearray()},
  "PCOMMAND_READ_BME_VALS":{'val':5,'responses':
	[
		ResponseElement("bme_T_C", 2,scale=10,signed=True),
		ResponseElement("bme_H_perc", 2,scale=10),
		ResponseElement("bme_P_inHg", 2,scale=100),
	],
	'default_payload':bytearray()},
  "PCOMMAND_READ_WIND_VALS":{'val':6,'responses':
	[
		ResponseElement("wind_A_deg", 2,scale=10),
		ResponseElement("wind_V_mph_last", 2,scale=10),
		ResponseElement("wind_V_mph", 2,scale=10),
		ResponseElement("rotor_mad_ms", 2,scale=10),
	],
	'default_payload':bytearray()},
  "PCOMMAND_READ_BOARD_T":{'val':7,'responses':
	[
		ResponseElement("board_T_C", 2,scale=10),
	],
	'default_payload':bytearray()},
  "PCOMMAND_NUM_READ_COMMANDS":{'val':8,'responses':
	[
	
	],
	'default_payload':bytearray()},
  "PCOMMAND_SET_ISR_LOW_COUNT":{'val':9,'responses':
	[
		ResponseElement("sent_isr_low_count", 2),
	],
	'default_payload':bytearray([100,0])},
  "PCOMMAND_SET_ISR_HIGH_COUNT":{'val':10,'responses':
	[
		ResponseElement("sent_isr_high_count", 2),
	],
	'default_payload':bytearray([100,0])},
  "PCOMMAND_SET_DAC_LEVEL":{'val':11,'responses':
	[
		ResponseElement("sent_dac_level_10bit", 2),
	],
	'default_payload':bytearray([100,2])},
  "PCOMMAND_SET_MOTOR_POSITION":{'val':12,'responses':
	[
		ResponseElement("sent_motor_position_deg", 2),
	],
	'default_payload':bytearray([95,0])},
  "PCOMMAND_SET_BACKLIGHT_LEVEL":{'val':13,'responses':
	[
		ResponseElement("sent_backlight_level_perc", 2),
	],
	'default_payload':bytearray([50,0])},
}

ACK = 0x06
NACK = 0x15

ap = argparse.ArgumentParser()

ap.add_argument('-d','--dest',help='x.x.x.x format IP address of destination',required=True)
ap.add_argument('-p','--port',help='Destination UDP port',required=False,default='8225')
ap.add_argument('-c','--command',help='Packet command byte',required=False,default=None)
ap.add_argument('-cv','--command_values',help='Command payload values (series of comma separated hex byte values',required=False,default="")
ap.add_argument('-n','--name',help='Command name',required=False,default='')
ap.add_argument('-nv','--value',help='Command value (if required)',required=False,default='')
ap.add_argument('-b','--base',help='Interpret command payload values as this base 16 is default, 10 is most likely other value ',required=False,default=16)
ap.add_argument('-ack','--ackbyte',help='Packet ack byte (for testing)',required=False,default=ACK)
ap.add_argument('-t','--timeout',help='Timeout in seconds (float format)',required=False,default="5.0")
ap.add_argument('-a','--all',help='Send every command and get responses',required=False,default=False,action='store_true')

clargs = ap.parse_args(sys.argv[1:])

sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

def pack16(val):
	ret = bytearray()
	if len(val) > 0:
		try:
			ival = int(float(val))
			ret.append(ival&255)
			ret.append((ival>>8)&255)
		except ValueError:
			print("pack16: can't convert {0}".format(val))
	return(ret)

def send_packet(command,command_payload_bytes):
	payload = bytearray()
	payload.append(int(clargs.ackbyte))
	payload.append(int(command))
	if len(command_payload_bytes) > 0:
		for b in command_payload_bytes:
			payload.append(int(b))
	else: # just send two zeros by default
		command_payload_bytes.append(0)
		command_payload_bytes.append(0)

	# add checksum bytes
	outgoing_checksum=0;
	for b in payload:
		outgoing_checksum+=int(b);
	
	payload.append(outgoing_checksum&255)
	payload.append(outgoing_checksum>>8)
	
	destination_addr = (clargs.dest,int(clargs.port))
	sock.settimeout(float(clargs.timeout))
	try:
		sock.sendto(payload,destination_addr)
		rec_msg, sender = sock.recvfrom(4096)
	except socket.timeout as to:
		print("Timeout after {0} s".format(float(clargs.timeout)))
		sys.exit(1)
	
	print("Sent: {0}".format(' '.join(['{0:x}'.format(x) for x in payload])))
	print("Received: {0}".format(' '.join(['{0:x}'.format(x) for x in rec_msg])))
	
	# validate checksum if needed
	if rec_msg[0]==ACK and len(rec_msg) > 2:
		computed_checksum = 0;
		for x in rec_msg[:-2]:
			computed_checksum += int(x)
	
		received_checksum = int(rec_msg[-2])+(int(rec_msg[-1])<<8)
		if received_checksum!=computed_checksum:
			print("Bad checksum: Received {0} vs Computed {1}".format(received_checksum,computed_checksum))
		else:
			print("Valid checksum: {0}".format(received_checksum))
	return(rec_msg[:-2])

def parse_received(rec):
	# parse results of commands
	parsed_ret = {}
	if len(rec) > 1: # got something back
		if rec[0]!=clargs.ackbyte:
			return(parsed_ret)
		command_key = ''
		for k in commands.keys():
			if rec[1]==commands[k]['val']:
				command_key = k
		if command_key in commands:
			expected_len=0
			for rel in commands[command_key]['responses']:
				expected_len+=rel.byte_number
			if len(rec)!=expected_len+2:
				raise ValueError("Unexpected packet len {0} (expected {1}) in command {2}".format(len(rec), expected_len,command_key))
			rec_idx=2
			for rel in commands[command_key]['responses']:
				parsed_ret[rel.name]=rel.parse(rec,rec_idx)
				rec_idx+=rel.byte_number
	return(parsed_ret)

##########################################
# Script start 
##########################################

if clargs.all: # send it all!
	for c in commands:
		print(c)
		print(parse_received(send_packet(commands[c]['val'],commands[c]['default_payload'])))

else:
	# given on command line
	command_payload_bytes=bytearray()
	if len(clargs.command_values) > 0:
		for cv in clargs.command_values.split(','):
			command_payload_bytes.append(int(cv,int(clargs.base)))
	
	rec = bytearray()
	
	# if specified with raw numerical values, send the packet
	if clargs.command!=None:
		rec = send_packet(clargs.command, command_payload_bytes)
	else:
		if len(clargs.name) > 0:
			# look for match in enum
			command_key=''
			for cn in commands.keys():
				if clargs.name.upper() in cn:
					command_key=cn
			if len(command_key) > 0:
				print("Command is {0} ({1})".format(command_key,commands[command_key]['val']))
				print(parse_received(send_packet(commands[command_key]['val'],pack16(clargs.value))))
			else:
				raise ValueError("Unknown command {0}".format(clargs.name)) 
		else:
			raise ValueError("Specify command via --command [number] or --name [name]")
	
