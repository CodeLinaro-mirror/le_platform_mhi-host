import os
import json
import argparse

def parse_txt(file_name,num,f2):
	search_string  = ["x" , "y" , "E"]
	found = False
	tab_char = "\t"
	final_res = []
	with open(file_name, "r") as f1:
		contents = f1.readlines()
		contents = contents[1:]

	for line in contents:
		start = line.find("L")
		result = []
		for label in search_string:
			if(line.find(label,start)!=-1):
				found = True
				start_base = line.find(label,start)
				end_base = line.find(',', start_base)
				value = line[start_base+3: end_base]
				result.append(value)

		result[0] = int(result[0])
		result[0] = (result[0] if result[0] <=32 else (result[0] -64))
		result[1] = (int(result[1])*num)
		result[2] = int(result[2])
		temp_res = [result[0] , result[1], result[2]]
		final_res.append(temp_res)

	return final_res

def main(args):

	file_name = ""
	tab_char = "\t"
	output_path = os.path.join(os.getcwd(), args.path, "eom_data.txt")
	f2 = open(output_path, 'w+')

	lane_list = [

	]

	result_dict = {
	"interface": "pcie",
	"instance":0,
	"time_scale": 1.953,
	"time_units": "ps",
	"voltage_scale": 1.7,
	"voltage_units": "mV",
	"note": "This can be per-domain info you want to highlight, e.g. speed x width, PHY settings version, part info, etc.",
	"lanes" : lane_list
	}

	my_dict = {
	"version" : "0.0.0",
	"results" : result_dict
	}


	for lane_number in range(0,16):

		lane_dict = {
		"lane_number": lane_number,
		"note": "This can be per-lane info you want to highlight",
		}


		filename = os.path.join(os.getcwd(),args.path,'positive_sequence_logs',f"lane{lane_number}.txt")
		pos_eye_data = parse_txt(filename,1,f2)
		filename = os.path.join(os.getcwd(),args.path,'negative_sequence_logs',f"lane{lane_number}.txt")
		neg_eye_data = parse_txt(filename,-1,f2)
		combine_data = pos_eye_data + neg_eye_data
		lane_dict['eye'] = combine_data
		lane_list.append (lane_dict)

	result_dict['lanes'] = lane_list
	result_list = []
	result_list.append(result_dict)
	my_dict ['results'] = result_list
	f2.write(json.dumps(my_dict,indent=4))
	f2.seek(0)
	lines = f2.readlines()
	f2.close()
	f2 = open(output_path, 'w+')
	lines = ['[EOMREPORTLIB]'+line for line in lines]
	f2.writelines(lines)

if __name__ == "__main__":
   # stuff only to run when not called via 'import' here
		parser = argparse.ArgumentParser(
							description='Eye diagram Generator')
		parser.add_argument("-folder",
							dest="path",
							help="(REQUIRED) Parent folder path where the logs are present",
							default="",
							required=True)
		args = parser.parse_args()
		main(args)
