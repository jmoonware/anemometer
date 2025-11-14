import socket
import argparse
import sys

ACK = 0x06
NACK = 0x15

ap = argparse.ArgumentParser()

ap.add_argument('-d','--dest',help='x.x.x.x format IP address of destination',required=True)
ap.add_argument('-p','--port',help='Destination UDP port',required=False,default='8225')
ap.add_argument('-c','--command',help='Packet command byte',required=False,default=1)
ap.add_argument('-cv','--command_values',help='Command payload values (series of comma separated hex byte values',required=False,default="")
ap.add_argument('-b','--base',help='Interpret command payload values as this base 16 is default, 10 is most likely other value ',required=False,default=16)
ap.add_argument('-a','--ackbyte',help='Packet ack byte (for testing)',required=False,default=ACK)

clargs = ap.parse_args(sys.argv[1:])

sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

# should do some error checking here...

msg = "010203".encode()

payload = bytearray()
payload.append(int(clargs.ackbyte))
payload.append(int(clargs.command))
if len(clargs.command_values) > 0:
	for cv in clargs.command_values.split(','):
		payload.append(int(cv,int(clargs.base)))
else:
	payload.append(0)
	payload.append(0)

# add checksum bytes
outgoing_checksum=0;
for b in payload:
	outgoing_checksum+=int(b);

payload.append(outgoing_checksum&255)
payload.append(outgoing_checksum>>8)

destination_addr = (clargs.dest,int(clargs.port))
sock.sendto(payload,destination_addr)

rec_msg, sender = sock.recvfrom(4096)
print("Sent: {0}".format(' '.join(['{0:x}'.format(x) for x in payload])))
print("Received: {0}".format(' '.join(['{0:x}'.format(x) for x in rec_msg])))

# validate checksum if needed
if rec_msg[0]==ACK and len(rec_msg) > 2:
	computed_checksum = sum(rec_msg[:-2])
	received_checksum = rec_msg[-2]+(rec_msg[-1]>>8)
	if received_checksum!=computed_checksum:
		print("Bad checksum: Received {0} vs Computed {1}".format(received_checksum,computed_checksum))
	else:
		print("Valid checksum: {0}".format(received_checksum))




